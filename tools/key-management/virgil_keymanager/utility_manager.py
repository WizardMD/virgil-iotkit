import base64
import copy
import io
import os
import shutil
import sys
from contextlib import contextmanager

from PyCRC.CRCCCITT import CRCCCITT
from prettytable import PrettyTable

from virgil_keymanager import consts
from virgil_keymanager.core_utils.virgil_time import date_to_timestamp
from virgil_keymanager.core_utils.helpers import b64_to_bytes

from virgil_keymanager.core_utils import DonglesCache, DongleChooser
from virgil_keymanager.core_utils.card_requests import CardRequestsHandler
from virgil_keymanager.external_utils.printer_controller import PrinterController
from virgil_keymanager.generators.trustlist import TrustListGenerator
from virgil_keymanager.generators.keys.atmel import AtmelKeyGenerator
from virgil_keymanager.generators.keys.virgil import VirgilKeyGenerator
from virgil_keymanager.storage import FileKeyStorage
from virgil_keymanager.storage.db_storage import DBStorage
from virgil_keymanager.storage.keys_tinydb_storage import KeysTinyDBStorage
from virgil_keymanager.storage.tinydb_storage_extensions import SignedByteStorage, CryptoByteStorage
from virgil_keymanager.storage.tl_version_tinydb_storage import TLVersionTinyDBStorage


class UtilityManager(object):

    def __init__(
            self,
            util_context
    ):
        self._context = util_context
        self.__ui = self._context.ui
        self.__logger = self._context.logger
        self.__key_storage_path = os.path.join(self._context.storage_path, "key_storage")
        self.__atmel = self._context.atmel
        self.__printer_controller = PrinterController(self.__ui)
        self.__upper_level_keys_count = 2
        self.__virgil_request_path = os.path.join(self.__key_storage_path, "virgil_requests")
        self._virgil_exporter_keys = None
        self.__dongles_cache = DonglesCache(self._context.disable_cache)
        self._utility_list = dict()
        self.__check_db_path()
        self.__logger.info("app started in main mode")

        # storage plugs
        self.__logger.info("key storage initialization")
        self.__logger.debug("keystorage at {}".format(self.__key_storage_path))
        self.__file_storage = FileKeyStorage(self.__key_storage_path)
        self.__logger.info("initialization successful")

        # Main db's plugs
        self.__upper_level_pub_keys = self.__init_storage(
            "UpperLevelKeys", storage_class=KeysTinyDBStorage, storage_type=SignedByteStorage, no_upper_level_db=True
        )
        self.__trust_list_pub_keys = self.__init_storage(
            "TrustListPubKeys", storage_class=KeysTinyDBStorage, storage_type=SignedByteStorage
        )
        self.__internal_private_keys = self.__init_storage(
            "InternalPrivateKeys", storage_class=KeysTinyDBStorage, storage_type=CryptoByteStorage
        )
        self.__factory_priv_keys = self.__init_storage(
            "FactoryPrivateKeys", storage_class=KeysTinyDBStorage, storage_type=CryptoByteStorage
        )
        self.__trust_list_version_db = self.__init_storage(
            "TrustListVersions", storage_class=TLVersionTinyDBStorage, storage_type=SignedByteStorage
        )

        # private keys db's for DevMode or no-dongles mode
        self.__firmware_priv_keys = self.__init_storage(
            "FirmwarePrivateKeys", storage_class=KeysTinyDBStorage, storage_type=CryptoByteStorage
        )
        self.__auth_private_keys = self.__init_storage(
            "AuthPrivateKeys", storage_class=KeysTinyDBStorage, storage_type=CryptoByteStorage
        )
        self.__recovery_private_keys = self.__init_storage(
            "RecoveryPrivateKeys", storage_class=KeysTinyDBStorage, storage_type=CryptoByteStorage
        )
        self.__trust_list_service_private_keys = self.__init_storage(
            "TLServicePrivateKeys", storage_class=KeysTinyDBStorage, storage_type=CryptoByteStorage
        )

        self.__keys_type_to_storage_map = {
            "auth": (self.__auth_private_keys, self.__upper_level_pub_keys),
            "firmware": (self.__firmware_priv_keys, self.__upper_level_pub_keys),
            "recovery": (self.__recovery_private_keys, self.__upper_level_pub_keys),
            "tl_service": (self.__trust_list_service_private_keys, self.__upper_level_pub_keys),
            "factory": (self.__factory_priv_keys, self.__trust_list_pub_keys)
        }
        self.__dongle_chooser = DongleChooser(
            self.__ui,
            self.__atmel,
            self.__dongles_cache,
            self.__upper_level_pub_keys,
            self.__trust_list_pub_keys,
            self.__logger
        )
        self.__logger.info("initialization successful")

        # generator plugs
        self.__logger.info("TrustList generator initialization")
        self.__trust_list_generator = TrustListGenerator(self.__ui, self.__trust_list_pub_keys, self.__atmel)
        self.__logger.info("initialization successful")

        # chooser
        if self._context.no_dongles:
            self.key_chooser = self.__db_key_chooser
        else:
            self.key_chooser = self.__dongle_key_chooser

        # card requests handler
        self.__card_requests_handler = CardRequestsHandler(
            self.__ui,
            self.__logger,
            self.__virgil_exporter_keys,
            path_to_requests_file=os.path.join(self.__virgil_request_path, "card_creation_requests.vr")
        )

    def __choose_dates_for_key(self, necessary: bool) -> (int, int):
        """
        Ask user to enter start/expiration dates for keys
        :param necessary: if False - user can skip specifying dates
        :return:          two timestamps, calculated with offset (see date_to_timestamp)
        """
        user_confirmed = False
        start_date = 0
        expiration_date = 0
        if necessary is False:
            user_confirmed = self.__ui.get_user_input(
                "Add start and expiration date for key? [y/n]: ",
                input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                input_checker_msg="Allowed answers [y/n]. Please try again: ",
                empty_allow=False
            ).upper()

        if necessary or (user_confirmed is True):
            self.__ui.print_message("Please choose start date for key")
            start_date = self.__ui.get_date()
            start_date = date_to_timestamp(*start_date)
            enter_expiration = self.__ui.get_user_input(
                "Enter expiration date? [y/n]: ",
                input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                input_checker_msg="Allowed answers [y/n]. Please try again: ",
                empty_allow=False
            ).upper()
            if enter_expiration == "Y":
                self.__ui.print_message("Please choose expiration date for key")
                expiration_date = self.__ui.get_date()
                expiration_date = date_to_timestamp(*expiration_date)

        return start_date, expiration_date

    def __init_storage(self, name, storage_class, storage_type, no_upper_level_db=False):

        @contextmanager
        def db_init_logs():
            self.__logger.info("{} initialization".format(name))
            self.__logger.debug("{} db at {}".format(name, storage_path))
            yield
            self.__logger.info("Initialization successful")

        storage_path = os.path.join(self.__key_storage_path, "db", name)
        with db_init_logs():
            if self._context.no_dongles:
                if name == "TrustListVersions":  # TODO: find better way for storage selection
                    return TLVersionTinyDBStorage(storage_path)
                return DBStorage(storage_path)
            upper_level_keys = None if no_upper_level_db else self.__upper_level_pub_keys
            return storage_class(
                storage_path,
                storage_type=storage_type,
                storage_kwargs={
                    "atmel": self.__atmel,
                    "ui": self.__ui,
                    "upper_level_keys_db": upper_level_keys
                }
            )

    def __dongle_key_chooser(self, key_type, stage, is_new_key=False, **kwargs):
        if is_new_key:
            dongle_serial = self.__dongle_chooser.choose_atmel_device("empty", **kwargs)
        else:
            dongle_serial = self.__dongle_chooser.choose_atmel_device(key_type, **kwargs)
        if not dongle_serial:
            self.__ui.print_warning("Operation stopped by user")
            self.__logger.info("Dongle has not been chosen ({})".format(stage))
            return
        return AtmelKeyGenerator(key_type, dongle_serial, self._context)

    def __db_key_chooser(self, key_type, stage, is_new_key=False, **kwargs):
        if is_new_key:
            return VirgilKeyGenerator(key_type)

        greeting_msg = kwargs.get("greeting_msg", "Please choose private key: ")
        suppress_db_warning = kwargs.get("suppress_db_warning", False)

        # filter keys from db by type
        private_keys_db = self.__keys_type_to_storage_map[key_type][0]
        public_keys_db = self.__keys_type_to_storage_map[key_type][1]

        private_keys = private_keys_db.get_all_data(suppress_db_warning=suppress_db_warning)
        public_keys = public_keys_db.get_all_data(suppress_db_warning=suppress_db_warning)

        filtered_private_keys = {key_id: info for key_id, info in private_keys.items() if info["type"] == key_type}
        filtered_public_keys = {key_id: info for key_id, info in public_keys.items() if info["type"] == key_type}

        if not len(filtered_private_keys):
            self.__ui.print_message(
                "Cannot find key with type [{}] inside {}. "
                "Please generate it.".format(key_type, os.path.basename(private_keys_db.storage_path))
            )
            self.__logger.info("Private key has not been selected ({})".format(stage))
            return

        # choose key id
        keys_info_list = []
        ids = sorted(filtered_private_keys)
        for key_id in ids:
            info_line = "db: {db}, type: {key_type}, comment: {key_comment}, key_id: {key_id}".format(
                db=os.path.basename(private_keys_db.storage_path),
                key_type=filtered_private_keys[key_id]["type"],
                key_comment=filtered_private_keys[key_id]["comment"],
                key_id=key_id
            )
            keys_info_list.append([info_line])
        self.__ui.print_message(greeting_msg)
        user_choose = self.__ui.choose_from_list(
            keys_info_list,
            "Please enter option number: ",
            "Keys list:"
        )
        key_id = ids[user_choose]

        return VirgilKeyGenerator(
            key_type,
            private_key=filtered_private_keys[key_id]["key"],
            public_key=filtered_public_keys[key_id]["key"]
        )

    def __generate_initial_keys(self):
        self.__logger.info("initial generation stage started")
        supress_db_warning = True
        upper_keys = self.__upper_level_pub_keys.get_all_data(suppress_db_warning=supress_db_warning)
        upper_keys_value = upper_keys.values()
        recovery_keys = list(filter(lambda x: x if x["type"] == "recovery" else None, upper_keys_value))

        if len(recovery_keys) >= self.__upper_level_keys_count:
            self.__ui.print_message("Infrastructure already exists. Do you want to DROP and re-create it?")
            user_choice = str(
                self.__ui.get_user_input(
                    "Drop infrastructure? [y/n]: ",
                    input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                    input_checker_msg="Allowed answers [y/n]. Please try again: ",
                    empty_allow=False
                )
            ).upper()
            if user_choice == "Y":
                self.__logger.info("key storage infrastructure cleaning")
                self.__ui.print_message("Cleaning all data...")
                self.__logger.debug("Removing {}".format(self.__key_storage_path))
                shutil.rmtree(self.__key_storage_path)
                self.__logger.debug("Creating {}".format(self.__key_storage_path))
                os.makedirs(os.path.join(self.__key_storage_path, "db"))
                private_keys_file_path = os.path.join(self.__key_storage_path, "private")
                public_keys_file_path = os.path.join(self.__key_storage_path, "pubkeys")
                for path in (private_keys_file_path, public_keys_file_path, self.__virgil_request_path):
                    if os.path.exists(path):
                        shutil.rmtree(path)
                dongle_directory = os.path.join(
                    os.getenv("HOME"), "DONGLES_{}".format("main")
                )
                self.__logger.debug("Use DONGLES directory: {}".format(dongle_directory))
                if os.path.exists(dongle_directory):
                    self.__logger.info("dongles emulator folder cleaning")
                    shutil.rmtree(dongle_directory)
                    self.__logger.info("dongles emulator folder cleaned")
                self.__dongles_cache.drop()
                self.__ui.print_message("Data cleaned")
                self.__logger.info("key storage cleaned")
            else:
                return
        for key_number in range(1, int(self.__upper_level_keys_count) + 1):
            self.__logger.info("Recovery Key {} generation (Initial Generation stage)".format(key_number))
            self.__ui.print_message("\nRecovery Key {}:".format(key_number))
            self.__generate_recovery_key(suppress_db_warning=supress_db_warning)

        for key_number in range(1, int(self.__upper_level_keys_count) + 1):
            self.__logger.info("Auth Key {} generation (Initial Generation stage)".format(key_number))
            self.__ui.print_message("\nAuth Key {}:".format(key_number))
            self.__generate_auth_key(suppress_db_warning=supress_db_warning)

        for key_number in range(1, int(self.__upper_level_keys_count) + 1):
            self.__logger.info("TrustList Service Key {} generation (Initial Generation stage)".format(key_number))
            self.__ui.print_message("\nTrustList Service Key {}:".format(key_number))
            self.__generate_trust_list_service_key(suppress_db_warning=supress_db_warning)

        for key_number in range(1, int(self.__upper_level_keys_count) + 1):
            self.__logger.info("Firmware Key {} generation (Initial Generation stage)".format(key_number))
            self.__ui.print_message("\nFirmware Key {}:".format(key_number))
            self.__generate_firmware_key()

        self.__logger.info("Factory Key generation (Initial Generation stage)")
        self.__ui.print_message("\nFactory Key:")
        self.__generate_factory_key()
        self.__logger.info("initial generation stage completed")

    def __generate_provision(self):
        self.__logger.info("provision SDMPD and sandbox generation started")
        self.__ui.print_message("Generating provision...")
        subfolder = "sdmpd_provision"
        sdmpd_prov_folder = os.path.join(self.__key_storage_path, subfolder)
        self.__logger.debug("Usign SDMPD provision folder: {}".format(sdmpd_prov_folder))

        # recreate folder
        if os.path.exists(sdmpd_prov_folder):
            self.__logger.info("provision folder deleted")
            shutil.rmtree(sdmpd_prov_folder)
        os.makedirs(sdmpd_prov_folder)
        self.__logger.info("provision folder created")

        # general actions
        file_storage = FileKeyStorage(sdmpd_prov_folder)
        self.__logger.info("UpperLevelPublicKeys dumping (provision and sandbox generation stage)")
        self.__dump_upper_level_pub_keys(subfolder)
        self.__logger.info("UpperLevelPublicKeys dump completed")
        self.__logger.info("TrustList generating (provision and sandbox generation stage)")
        self.__generate_trust_list(file_storage)
        self.__logger.info("TrustList generation completed")
        self.__logger.info("generating SDMPD Key")
        key_pair = self.__generate_sdmpd_key()

        factory_key = self.key_chooser(
            "factory",
            stage="Generate provision, Factory Key choosing stage",
            greeting_msg="Please choose Factory Key for signing: "
        )
        if not factory_key:
            return

        key_to_be_signed = key_pair.public_key
        sign_result = factory_key.sign(key_to_be_signed)
        signature = b64_to_bytes(sign_result)
        factory_key_id = factory_key.key_id

        self.__logger.info("Factory signature and key pair dumping")
        file_storage.save(signature, "signature_" + str(factory_key_id))
        file_storage.save(b64_to_bytes(key_pair.private_key), "device_priv_" + str(factory_key_id))
        file_storage.save(b64_to_bytes(key_pair.public_key), "device_pub_" + str(factory_key_id))
        self.__logger.info("Factory signature and key pair dump completed")
        self.__ui.print_message("Generation provision done")

    def __dump_upper_level_pub_keys(self, filename="pubkeys"):
        self.__logger.info("UpperLevelPublic Keys dumping")
        self.__ui.print_message("Dumping upper level Public Keys")
        pubkey_dir = os.path.join(self.__key_storage_path, filename)
        if not os.path.exists(pubkey_dir):
            os.mkdir(pubkey_dir)

        storage = self.__upper_level_pub_keys
        for key_id in storage.get_keys():
            key_data = storage.get_value(key_id)
            if key_data["type"] == "recovery":
                self.__save_key(
                    key_id,
                    key_data["key"],
                    os.path.join(pubkey_dir, key_data["type"] + "_" + key_id + "_" + str(key_data["comment"]) + ".pub")
                )
            else:
                self.__save_key(
                    key_id,
                    key_data["key"],
                    os.path.join(pubkey_dir, key_data["type"] + "_" + key_id + "_" + str(key_data["comment"]) + ".pub"),
                    signer_key_id=key_data["signer_key_id"],
                    signature=key_data["signature"]
                )
        self.__ui.print_message("Keys dump finished")
        self.__logger.info("UpperLevelPublicKeys dumping completed")

    def __generate_trust_list(self, storage=None):
        trust_list_types = [["Dev"], ["Release"]]
        trust_type_choice_raw = self.__ui.choose_from_list(
            trust_list_types, "Please choose TrustList type: ",
            "TrustList types:"
        )
        trust_type_choice = trust_list_types[trust_type_choice_raw][0]
        self.__logger.info("{} TrustList generation started".format(trust_type_choice))
        self.__ui.print_message("\nGenerating {} TrustList...".format(trust_type_choice))

        if trust_type_choice == "Dev":
            current_tl_version = self.__trust_list_version_db.get_dev_version()

            self.__ui.print_message("Current TrustList version is {}".format(current_tl_version))
            tl_version = self.__ui.get_user_input(
                "Enter the TrustList version [{}]: ".format(current_tl_version + 1),
                input_checker_callback=self.__ui.InputCheckers.number_check,
                input_checker_msg="Only the integer value is allowed. Please try again: ",
                empty_allow=True
            )
            if not tl_version:
                tl_version = current_tl_version + 1
            else:
                tl_version = int(tl_version)
            if tl_version != current_tl_version + 1:
                user_choice = str(
                    self.__ui.get_user_input(
                        "Are you sure want change current TrustList version to {} [y/n]: ".format(tl_version),
                        input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                        input_checker_msg="Allowed answers [y/n]. Please try again: ",
                        empty_allow=False
                    )
                ).upper()
                if user_choice == "N":
                    self.__ui.print_warning("Operation stopped by user, TrustList version doesn't changed")
                    self.__logger.info("TrustList version doesn't changed")
                    return

            self.__trust_list_version_db.save("dev_version", tl_version)
        else:
            current_tl_version = self.__trust_list_version_db.get_release_version()

            self.__ui.print_message("Current TrustList version is {}".format(current_tl_version))
            tl_version = self.__ui.get_user_input(
                "Enter the TrustList version [{}]: ".format(current_tl_version + 1),
                input_checker_callback=self.__ui.InputCheckers.number_check,
                input_checker_msg="Only the integer value is allowed. Please try again: ",
                empty_allow=True
            )
            if not tl_version:
                tl_version = current_tl_version + 1
            else:
                tl_version = int(tl_version)
            if tl_version != current_tl_version + 1:
                user_choice = str(
                    self.__ui.get_user_input(
                        "Are you sure want change current TrustList version to {} [y/n]: ".format(tl_version),
                        input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                        input_checker_msg="Allowed answers [y/n]. Please try again: ",
                        empty_allow=False
                    )
                ).upper()
                if user_choice == "N":
                    self.__ui.print_warning("Operation stopped by user, TrustList version doesn't changed")
                    self.__logger.info("TrustList version doesn't changed")
                    return

            self.__trust_list_version_db.save("release_version", tl_version)

        auth_key = self.key_chooser(
            "auth",
            stage="TrustList generation, Auth Key choosing",
            greeting_msg="Please choose Auth Key for TrustList signing: "
        )
        if not auth_key:
            return
        tl_key = self.key_chooser(
            "tl_service",
            stage="TrustList generation, TrustList Service Key choosing",
            greeting_msg="Please choose TrustList Service Key for TrustList signing: "
        )
        if not tl_key:
            return

        self.__logger.info("TrustList version: {} ".format(tl_version))

        signer_keys = [auth_key, tl_key]
        tl = self.__trust_list_generator.generate(
            signer_keys,
            tl_version,
            trust_type_choice == "Dev"
        )
        self.__ui.print_message("Generation finished")
        self.__ui.print_message("Storing to file...")
        if not storage:
            if trust_type_choice == "Dev":
                trust_list_storage_path = os.path.join(self.__key_storage_path, "trust_lists", "dev")
            else:
                trust_list_storage_path = os.path.join(self.__key_storage_path, "trust_lists", "release")
            storage = FileKeyStorage(trust_list_storage_path)
        storage.save(tl, "TrustList")
        self.__ui.print_message("File stored")
        self.__ui.print_message("TrustList generated and stored")
        self.__logger.info("TrustList generation completed")

    def __generate_recovery_key(self, suppress_db_warning=False):
        self.__logger.info("Recovery Key generation started")
        self.__ui.print_message("Generating Recovery Key...")
        upper_keys = self.__upper_level_pub_keys.get_all_data(suppress_db_warning=suppress_db_warning)
        upper_keys_value = upper_keys.values()
        recovery_keys = list(filter(lambda x: x if x["type"] == "recovery" else None, upper_keys_value))

        recovery_key = self.key_chooser(
            "recovery", stage="Recovery Key generation", is_new_key=True, suppress_db_warning=suppress_db_warning
        )
        if not recovery_key:
            return

        if not recovery_key.generate():
            self.__ui.print_message("Recovery Key generation failed")
            self.__logger.error("Recovery Key generation failed")
            return

        public_key = recovery_key.public_key
        key_id = recovery_key.key_id

        comment = 0
        if len(recovery_keys) == 0:
            comment = 1
        elif len(recovery_keys) == 1:
            rec_comment = recovery_keys[0]["comment"]
            if rec_comment == 1:
                comment = 2
            elif rec_comment == 2:
                comment = 1
        elif len(recovery_keys) > 1:
            self.__logger.error("third Recovery Key generation attempt")
            sys.exit("You try to generate the third Recovery Key, it's a very bad situation!")
        else:
            self.__logger.error("unexpected error in Recovery Key comment")
            sys.exit("Something went wrong with the Recovery Key comment setup")

        key_info = {
            "type": "recovery",
            "comment": comment,
            "key": public_key
        }
        self.__upper_level_pub_keys.save(str(key_id), key_info, suppress_db_warning=suppress_db_warning)
        key_info["key"] = recovery_key.private_key
        if self._context.no_dongles:
            self.__recovery_private_keys.save(str(key_id), key_info, suppress_db_warning=suppress_db_warning)
            self.__card_requests_handler.create_and_save_request_for_key(recovery_key, {"sample info": "key info"})
        if self._context.dongles_mode == "dongles":
            self.__dongle_unplug_and_mark(recovery_key.device_serial, "recovery")
        if not self._context.printer_disable:
            self.__printer_controller.send_to_printer(key_info)
        self.__ui.print_message("Generation finished")
        self.__logger.info("Recovery Key id: [{id}] comment: [{comment}] generation completed".format(
            id=key_id,
            comment=comment
        ))

    def __generate_upper_level_signable_key(self, key_type, name, suppress_db_warning):
        """
        Generate auth, trust list service or firmware key, which are signed by recovery key
        """
        self.__logger.info("{} Key generation started".format(name))
        self.__ui.print_message("Generating {} Key...".format(name))
        key = self.key_chooser(
            key_type, stage="{} generation".format(name), is_new_key=True, suppress_db_warning=suppress_db_warning
        )
        if not key:
            return

        rec_pub_keys = self.__get_recovery_pub_keys(suppress_db_warning=suppress_db_warning)
        rec_key_for_sign = self.key_chooser(
            "recovery",
            stage="{} Key generation, Recovery key for signing selection".format(name),
            greeting_msg="Please choose Recovery Key for signing: ",
            suppress_db_warning=suppress_db_warning
        )
        if not rec_key_for_sign:
            return

        if not key.generate(rec_pub_keys=rec_pub_keys, signer_key=rec_key_for_sign):
            self.__logger.info("{} Key generation failed".format(name))
            self.__ui.print_message("{} Key generation failed".format(name))
            return

        # getting sign
        sign = key.signature

        signer_key_id = rec_key_for_sign.key_id
        key_id = key.key_id
        comment = self.__ui.get_user_input("Enter comment for {} Key: ".format(name))
        key_info = {
            "type": key.key_type,
            "comment": comment,
            "key": key.public_key,
            "signature": sign,
            "signer_key_id": signer_key_id
        }
        self.__upper_level_pub_keys.save(str(key_id), key_info, suppress_db_warning=suppress_db_warning)
        key_info["key"] = key.private_key
        if self._context.no_dongles:
            private_keys_db = self.__keys_type_to_storage_map[key_type][0]
            private_keys_db.save(str(key_id), key_info, suppress_db_warning=suppress_db_warning)
        self.__card_requests_handler.create_and_save_request_for_key(key, key_info={"sample info": "key info"})
        if self._context.dongles_mode == "dongles":
            self.__dongle_unplug_and_mark(key.device_serial, key_type)
        if not self._context.printer_disable:
            self.__printer_controller.send_to_printer(key_info)
        self.__ui.print_message("Generation finished")
        self.__logger.info("{key_name} Key id: [{id}] comment: [{comment}] generation completed".format(
            key_name=name,
            id=key_id,
            comment=comment
        ))

    def __revive_recovery_key(self):
        self.__logger.info("Recovery Key revival started")
        self.__revive_key("recovery")

    def __generate_sdmpd_key(self):
        self.__ui.print_message("\nGenerating SDMPD Key...")
        key_pair = VirgilKeyGenerator("sdmpd").generate()
        self.__ui.print_message("Generation finished")
        self.__ui.print_message("Auth and TL Service dongles can be unplugged")
        return key_pair

    def __generate_factory_key(self):
        self.__logger.info("Factory Key generation started")
        self.__ui.print_message("\nGenerating Factory Key...")
        factory_key = self.key_chooser("factory", is_new_key=True, stage="Factory Key generation")
        if not factory_key:
            return
        signature_limit = self.__ui.get_user_input(
            "Enter the signature limit number from 1 to 4294967295 [4294967295]: ",
            input_checker_callback=self.__ui.InputCheckers.signature_input_check,
            input_checker_msg="Only the number in range from 1 to 4294967295 is allowed. Please try again: ",
            empty_allow=True
        )
        rec_pub_keys = self.__get_recovery_pub_keys()
        if not factory_key.generate(signature_limit=signature_limit, rec_pub_keys=rec_pub_keys):
            self.__logger.error("Factory Key generation failed")
            sys.exit("Factory Key generation failed")

        # Choose start/expiration dates
        start_date, expiration_date = self.__choose_dates_for_key(necessary=True)

        key_id = factory_key.key_id
        ec_type = factory_key.ec_type
        factory_name = self.__ui.get_user_input("Enter factory name: ", empty_allow=False)
        key_info = {
            "type": "factory",
            "ec_type": ec_type,
            "start_date": start_date,
            "expiration_date": expiration_date,
            "comment": factory_name,
            "key": factory_key.private_key,
            "public_key": factory_key.public_key
        }
        self.__factory_priv_keys.save(str(key_id), key_info)
        key_info["key"] = key_info["public_key"]
        del key_info["public_key"]
        self.__trust_list_pub_keys.save(str(key_id), key_info)
        self.__card_requests_handler.create_and_save_request_for_key(factory_key, {"sample info": "key info"})

        if self._context.dongles_mode == "dongles":
            self.__dongle_unplug_and_mark(factory_key.device_serial, "factory")

        self.__ui.print_message("Generation finished")
        self.__logger.info(
            "Factory Key id: [{key_id}] signature limit: [{signature_limit}] comment: [{comment}]"
            " start date: [{start_date}] expiration_date: [{expiration_date}] generation completed".format(
                key_id=key_id,
                signature_limit=signature_limit or "unlimited",
                comment=factory_name,
                start_date=start_date,
                expiration_date=expiration_date
            ))

    def __re_generate_factory_key(self):
        self.__logger.info("re-generation of Factory Key dongle from the existing key")
        self.__ui.print_message("Creating Factory dongle from the existing Key...")
        factory_key = self.key_chooser("factory", is_new_key=True, stage="re-generation of Factory Key")
        factory_private_keys = list(
            map(lambda x: ["factory: {}".format(x["comment"]), x], self.__factory_priv_keys.get_values())
        )
        if not factory_private_keys:
            self.__logger.error("no Keys were found, re-generation failed")
            self.__ui.print_error("Can't find any Factory Keys")
            return
        chosen_key = self.__ui.choose_from_list(factory_private_keys, "Please enter option number: ", "Factory Keys:")
        signature_limit = self.__ui.get_user_input(
            "Enter the signature limit number from 1 to 4294967295 [4294967295]: ",
            input_checker_callback=self.__ui.InputCheckers.signature_input_check,
            input_checker_msg="Only the number in range from 1 to 4294967295 is allowed. Please try again: ",
            empty_allow=True
        )
        rec_pub_keys = self.__get_recovery_pub_keys()
        if not factory_key.generate(
                signature_limit=signature_limit,
                rec_pub_keys=rec_pub_keys,
                private_key_base64=factory_private_keys[chosen_key][1]["key"]
        ):
            self.__logger.error("Factory Key generation failed")
            sys.exit("Factory Key generation failed")
        if self._context.dongles_mode == "dongles":
            self.__dongle_unplug_and_mark(factory_key.device_serial, "factory")
        key_id = factory_key.key_id
        self.__ui.print_message("Generation finished")
        self.__logger.info(
            "Factory key id: [{key_id}] signature limit: [{signature_limit}] comment: [{comment}]"
            " re-generation finished".format(
                key_id=key_id,
                signature_limit=signature_limit or "unlimited",
                comment=factory_private_keys[chosen_key][1]["comment"]
        ))

    def __delete_factory_key(self):
        self.__logger.info("Factory Key deleting")
        self.__ui.print_message("Deleting Factory Key...")
        trust_list_pub_keys = self.__trust_list_pub_keys.get_all_data()
        factory_keys_info = list()
        for tl_list_key in trust_list_pub_keys:
            if trust_list_pub_keys[tl_list_key]["type"] == "factory":
                factory_keys_info.append(
                    ["factory_name: {}".format(trust_list_pub_keys[tl_list_key]["comment"]), tl_list_key]
                )
        if not factory_keys_info:
            self.__logger.info("No Factory Key was found. Nothing to delete")
            self.__ui.print_warning("No Factory Key was found. Nothing to delete")
            return
        user_choice = self.__ui.choose_from_list(
            factory_keys_info,
            "Please choose Factory Key to delete: ",
            "Factory Keys: "
        )
        self.__trust_list_pub_keys.delete_key(factory_keys_info[user_choice][1])
        if factory_keys_info[user_choice][1] in self.__factory_priv_keys.get_keys():
            self.__factory_priv_keys.delete_key(factory_keys_info[user_choice][1])
        self.__ui.print_message("Factory Key deleted")
        key_id = factory_keys_info[user_choice][1]
        self.__logger.info("Factory Key with id: [{}] deleted".format(key_id))

    def __generate_auth_key(self, suppress_db_warning=False):
        self.__generate_upper_level_signable_key("auth", "Auth", suppress_db_warning)

    def __generate_internal_key(self, key_type, name_for_log):
        self.__logger.info("%s Key generation started" % name_for_log)
        self.__ui.print_message("\nGenerating %s Key..." % name_for_log)

        key_pair = VirgilKeyGenerator(key_type).generate()
        public_key = key_pair.public_key
        key_id = key_pair.key_id
        comment = self.__ui.get_user_input("Enter comment for %s Key: " % name_for_log)
        key_info = {
            "type": key_pair.key_type,
            "ec_type": key_pair.ec_type,
            "start_date": 0,
            "expiration_date": 0,
            "comment": comment,
            "key": public_key
        }
        self.__trust_list_pub_keys.save(str(key_id), key_info)
        key_info["key"] = key_pair.private_key
        self.__internal_private_keys.save(str(key_id), key_info)
        self.__card_requests_handler.create_and_save_request_for_key(key_pair, {"sample info": "key info"})
        self.__ui.print_message("Generation finished")
        self.__logger.info("{name} Key id: [{id}] comment: [{comment}] generation completed".format(
            name=name_for_log,
            id=key_id,
            comment=comment
        ))

    def __generate_auth_internal_key(self):
        self.__generate_internal_key("auth_internal", "AuthInternal")

    def __generate_firmware_internal_key(self):
        self.__generate_internal_key("firmware_internal", "FirmwareInternal")

    def __revive_auth_key(self):
        self.__revive_key("auth")

    def __generate_trust_list_service_key(self, suppress_db_warning=False):
        self.__generate_upper_level_signable_key("tl_service", "TrustList Service", suppress_db_warning)

    def __revive_tl_service_key(self):
        self.__revive_key("tl_service")

    def __generate_firmware_key(self, suppress_db_warning=False):
        self.__generate_upper_level_signable_key("firmware", "Firmware", suppress_db_warning)

    def __revive_firmware_key(self):
        self.__revive_key("firmware")

    def __manual_add_public_key(self):
        self.__logger.info("Adding Public Key to db")
        self.__ui.print_message("Manual adding Public Key to db...")
        key_type_list = [
            ["factory", self.__trust_list_pub_keys],
        ]

        type_choice = self.__ui.choose_from_list(key_type_list, "Please choose Key type: ", "Key types:")
        public_key = self.__ui.get_user_input(
            "Enter Public Key (tiny base64): ",
            input_checker_callback=self.__ui.InputCheckers.check_base64,
            input_checker_msg="Can't decode, please ensure it's base64 and try again"
        )
        comment = self.__ui.get_user_input("Enter comment for [{}] Key: ".format(key_type_list[type_choice][0]))
        key_id = CRCCCITT().calculate(base64.b64decode(public_key))

        key_data = {
            "type": key_type_list[type_choice][0],
            "comment": comment,
            "key": public_key
        }
        key_type_list[type_choice][1].save(str(key_id), key_data)
        self.__logger.info("[{key_type}] Key id: [{key_id}] was added to db".format(
            key_type=key_type_list[type_choice][0],
            key_id=key_id
        ))
        self.__ui.print_message("Key added")

    def __revive_key(self, key_type):

        def check_key_info(check_id, check_info):
            # type: (str, dict) -> bool
            key_info_from_db = self.__upper_level_pub_keys.get_value(str(check_id))
            if not key_info_from_db:
                self.__ui.print_error("Restored data doesn't match the data in the UpperLevelPubKeys db!")
                return False
            else:
                key_info_from_db["comment"] = str(key_info_from_db["comment"])
                if key_type != "recovery":
                    del key_info_from_db["signature"]
                    del key_info_from_db["signer_key_id"]
            if check_info != key_info_from_db:
                self.__ui.print_error("Restored data doesn't match the data in the UpperLevelPubKeys db!")
                return False
            return True

        self.__logger.info("Revive [{}] Key started".format(key_type))
        self.__ui.print_message("Reviving [{}] Key...".format(key_type))
        key_to_revive = self.key_chooser(key_type, is_new_key=True, stage="Reviving [{}] key".format(key_type))
        if not key_to_revive:
            return

        private_key_base64 = self.__ui.get_user_input(
            "Enter Private Key from the RestorePaper: ",
            input_checker_callback=self.__ui.InputCheckers.check_base64,
            input_checker_msg="Can't decode, please ensure it's base64 and try again",
            empty_allow=False
        )
        if key_type == 'recovery':
            comment = self.__ui.get_user_input(
                "Enter comment for [{}] Key from RestorePaper: ".format(key_type),
                input_checker_callback=self.__ui.InputCheckers.check_recovery_key_comment,
                input_checker_msg="Allowed comment [1 or 2]. Please try again: "
            )
        else:
            comment = self.__ui.get_user_input("Enter comment for [{}] Key from RestorePaper: ".format(key_type))

        revive_result = None
        if key_type == "recovery":
            revive_result = key_to_revive.generate(private_key_base64=private_key_base64)
        else:
            rec_pub_keys = self.__get_recovery_pub_keys()
            rec_key_for_sign = self.key_chooser("recovery", stage="Reviving [{}] key, select signer".format(key_type))
            if key_type in ("auth", "tl_service", "firmware"):
                revive_result = key_to_revive.generate(
                    rec_pub_keys=rec_pub_keys,
                    signer_key=rec_key_for_sign,
                    private_key_base64=private_key_base64
                )

        if not revive_result:
            self.__logger.info("Restoration failed")
            self.__ui.print_error("Restoration failed")
            return
        # TODO: use dongles cache only in dongles mode
        # cache_info = self.__dongles_cache.search_serial(device_serial)
        # if cache_info:
        #     public_key = cache_info["key"]
        #     key_id = cache_info["key_id"]
        # else:
        #     ops_status = self.__atmel.get_public_key(device_serial)
        #     if not ops_status[0]:
        #         self.__logger.error("reviving failed: {}".format(ops_status[1]))
        #         sys.exit(ops_status[1])
        #     public_key = ops_status[1]
        #     key_id = str(CRCCCITT().calculate(base64.b64decode(public_key)))
        # del cache_info
        public_key = key_to_revive.public_key
        key_id = key_to_revive.key_id

        key_info = {
            "type": key_type,
            "comment": comment,
            "key": public_key
        }
        if not check_key_info(key_id, key_info):
            return
        self.__logger.info("[{key_type}] Key id: [{key_id}] comment: [{comment}] restoration completed".format(
            key_type=key_type,
            key_id=key_id,
            comment=comment
        ))
        self.__ui.print_message("Restoration finished.")

    def __print_all_pub_keys_db(self):
        self.__logger.info("Printing Public Keys from db's started")
        self.__ui.print_message("Printing Public Keys from db's...")
        self.__ui.print_message("\nUpper level Keys: ")
        pt = PrettyTable(["Key Id", "Type", "Comment", "Signed by", "Key"])
        for key in self.__upper_level_pub_keys.get_keys():
            row = self.__upper_level_pub_keys.get_value(key)
            signer_id = row.get("signer_key_id", "")
            pt.add_row([key, row["type"], row["comment"], signer_id, row["key"]])
        self.__ui.print_message(pt.get_string())
        self.__logger.debug("\nUpper level Keys: \n{}".format(pt.get_string()))
        del pt

        self.__ui.print_message("\nTrustList Service Keys: ")
        pt = PrettyTable(["Key Id", "Type", "EC type", "Comment", "Start Date", "Expiration Date", "Key"])
        for key in self.__trust_list_pub_keys.get_keys():
            row = self.__trust_list_pub_keys.get_value(key)
            pt.add_row(
                [key, row["type"], row["ec_type"], row["comment"], row["start_date"], row["expiration_date"], row["key"]]
            )
        self.__ui.print_message(pt.get_string())
        self.__logger.debug("\nTrustList Service Keys: \n{}".format(pt.get_string()))
        del pt
        self.__logger.info("Printing Public Keys from db's completed")

    def __generate_recovery_by_count(self):
        self.__generate_keys_by_count("recovery")

    def __generate_auth_by_count(self):
        self.__generate_keys_by_count("auth")

    def __generate_tl_key_by_count(self):
        self.__generate_keys_by_count("tl_service")

    def __generate_firmware_by_count(self):
        self.__generate_keys_by_count("firmware")

    def __generate_keys_by_count(self, key_type):
        upper_level_keys = self.__upper_level_pub_keys.get_all_data()
        key_type_keys_info = dict()
        for key in upper_level_keys.keys():
            if upper_level_keys[key]["type"] == key_type:
                key_type_keys_info[key] = upper_level_keys[key]

        # clean old keys
        for key in key_type_keys_info.keys():
            self.__upper_level_pub_keys.delete_key(key)
            if key_type == "tl_service":
                if key in self.__trust_list_service_private_keys.get_keys():
                    self.__trust_list_service_private_keys.delete_key(key)
            if key_type == "factory":
                if key in self.__factory_priv_keys.get_keys():
                    self.__factory_priv_keys.delete_key(key)
            if self._context.no_dongles and key_type == "recovery":
                if key in self.__recovery_private_keys.get_keys():
                    self.__recovery_private_keys.delete_key(key)
            if self._context.no_dongles and key_type == "auth":
                if key in self.__auth_private_keys.get_keys():
                    self.__auth_private_keys.delete_key(key)

        # clean cache
        self.__dongles_cache.drop()

        if key_type == "recovery":
            for key_number in range(1, int(self.__upper_level_keys_count) + 1):
                self.__ui.print_message("\nGenerate Recovery Key {}:".format(key_number))
                self.__generate_recovery_key()
        if key_type == "auth":
            for key_number in range(1, int(self.__upper_level_keys_count) + 1):
                self.__ui.print_message("\nGenerate Auth Key {}:".format(key_number))
                self.__generate_auth_key()
        if key_type == "tl_service":
            for key_number in range(1, int(self.__upper_level_keys_count) + 1):
                self.__ui.print_message("\nGenerate TrustList Service Key {}:".format(key_number))
                self.__generate_trust_list_service_key()
        if key_type == "firmware":
            for key_number in range(1, int(self.__upper_level_keys_count) + 1):
                self.__ui.print_message("\nGenerate Firmware Key {}:".format(key_number))
                self.__generate_firmware_key()

    def __restore_upper_level_db_from_keys(self):
        self.__logger.info("UpperLevelPubKeys db restoration started")
        self.__ui.print_message("Restoring db from dongles...")

        # get key type
        key_type_list = [
            ["recovery"],
            ["auth"],
            ["firmware"],
            ["tl_service"]
        ]
        type_choice = self.__ui.choose_from_list(key_type_list, "Please choose Key type: ", "Key types:")

        # get chosen device serial, and start build real db record, from device info map
        choosed_device = self.key_chooser(
            key_type_list[type_choice][0],
            stage="UpperLevelPubKeys db restoration",
            hw_info=True,
            suppress_db_warning=True
        )
        rec_key_for_sign = 0
        if key_type_list[type_choice][0] in ["auth", "tl_service", "firmware"]:
            rec_key_for_sign = self.key_chooser(
                "recovery",
                stage="UpperLevelPubKeys db restoration",
                greeting_msg="Please choose Recovery Key for signing {}:".format(key_type_list[type_choice][0])
            )

        # get devices info list
        ops_status = self.__atmel.list_devices()
        if not ops_status[0]:
            self.__logger.error("restoration failed: {}".format(ops_status[1]))
            sys.exit(ops_status[1])
        device_list_info = self.__dongle_chooser.list_devices_info_hw(ops_status[1])
        del ops_status

        # build device map closely related to db records
        device_info_map = dict()
        for dev_info in device_list_info:
            cleaned_dev_info = copy.copy(dev_info)
            del cleaned_dev_info["device_serial"]
            device_info_map[dev_info["device_serial"]] = cleaned_dev_info

        if choosed_device in device_info_map.keys():
            db_row = device_info_map[choosed_device]
            if key_type_list[type_choice][0] == "recovery":
                db_row["comment"] = self.__ui.get_user_input(
                    "Enter comment for {} Key: ".format(key_type_list[type_choice][0]),
                    input_checker_callback=self.__ui.InputCheckers.check_recovery_key_comment,
                    input_checker_msg="Allowed comment [1 or 2]. Please try again: "
                )
            else:
                db_row["comment"] = self.__ui.get_user_input(
                    "Enter comment for {} Key: ".format(key_type_list[type_choice][0]),
                )
            key_id = db_row["key_id"]
            if "public_key" in db_row.keys():
                db_row["key"] = db_row["public_key"]
                del db_row["public_key"]

            del db_row["key_id"]

            ops_status = self.__atmel.sign_by_device(db_row["key"], device_serial=rec_key_for_sign)
            if not ops_status[0]:
                self.__logger.error("generation failed: {}".format(ops_status[1]))
                sys.exit(ops_status[1])
            db_row["signature"] = ops_status[1]
            del ops_status

            ops_status = self.__atmel.get_public_key(device_serial=rec_key_for_sign)
            if not ops_status[0]:
                self.__logger.error("generation failed: {}".format(ops_status[1]))
                sys.exit(ops_status[1])
            signer_id = CRCCCITT().calculate(base64.b64decode(ops_status[1]))
            db_row["signer_key_id"] = signer_id
            del ops_status
            if key_type_list[type_choice][0] == "recovery":
                self.__upper_level_pub_keys.save(key_id, db_row, suppress_db_warning=True)
            else:
                self.__upper_level_pub_keys.save(key_id, db_row)
        else:
            self.__ui.print_error("Unknown device")
            return
        self.__logger.info("[{key_type}] Key id: [{key_id}] comment: [{comment}] restoration completed".format(
            key_type=key_type_list[type_choice][0],
            key_id=key_id,
            comment=db_row["comment"]
        ))
        self.__ui.print_message("Key restored")

    def __import_trust_list_to_db(self):
        trust_list_folder = os.path.join(self.__key_storage_path, "trust_lists")
        if os.path.exists(trust_list_folder):
            trust_list_files = os.listdir(trust_list_folder)
            if len(trust_list_files) == 0:
                self.__logger.error("TrustList storage are empty at path: {}".format(trust_list_folder))
                self.__ui.print_error("TrustList storage are empty at path: {}".format(trust_list_folder))

            first_trust_list_file = None

            for file in trust_list_files:
                if "TrustList_" in file and ".tl" in file:
                    first_trust_list_file = file
                    break

            if not first_trust_list_file:
                self.__logger.error("Not found any TrustList in storage at {}".format(trust_list_folder))
                self.__ui.print_error("Not found any TrustList in storage at {}".format(trust_list_folder))
                return

            self.__logger.debug("First finded trustlist: {}".format(first_trust_list_file))


            trust_list_file = open(
                os.path.join(trust_list_folder, first_trust_list_file),
                "rb"
            )
            self.__logger.debug("Strat handlind finded TrustList file")

            trust_list_file.seek(6)
            pub_keys_count = int.from_bytes(
                trust_list_file.read(2),
                byteorder='little',
                signed=False
            )
            self.__logger.debug("Finded TrustList have {} key(s)".format(pub_keys_count))
            trust_list_file.seek(32)
            key_type_names = {
                int(consts.VSKeyTypeE.FACTORY): "factory",
                int(consts.VSKeyTypeE.FIRMWARE): "firmware",
                int(consts.VSKeyTypeE.FIRMWARE_INTERNAL): "firmware_internal",
                int(consts.VSKeyTypeE.AUTH): "auth_internal"
            }
            while pub_keys_count > 0:
                key_data = trust_list_file.read(96)
                self.__logger.debug("Data bytearray size: {}".format(len(key_data)))
                key_type_int = int.from_bytes(key_data[66:68], byteorder='little', signed=False)
                key_id = int.from_bytes(key_data[64:66], byteorder='little', signed=False)
                key_type_name = key_type_names[key_type_int]
                key_bytes_base64 = base64.b64encode(bytes(key_data[:64])).decode()
                if key_type_int not in list(key_type_names.keys()) or self.__trust_list_pub_keys.get_value(key_id):
                    self.__logger.debug("Key with key type number {} skipped".format(key_type_int))
                    pub_keys_count -= 1
                    continue
                key_data_message = "Importing Key type: {key_type} id: {key_id} content: {key_base64}".format(
                    key_id=key_id,
                    key_type=key_type_name,
                    key_base64=key_bytes_base64
                )
                self.__ui.print_message(key_data_message)
                self.__logger.debug(key_data_message)
                key_comment = self.__ui.get_user_input(
                    "Enter {} key comment: ".format(key_type_names[key_type_int]),
                    empty_allow=False
                )
                self.__logger.debug("Adding comment: {} to key".format(key_comment))
                key_info = {
                    "type": key_type_name,
                    "comment": key_comment,
                    "key": key_bytes_base64,
                }
                self.__trust_list_pub_keys.save(str(key_id), key_info)
                self.__logger.debug("{key_type} Key with id: {key_id} added to TrustListPubKeys db".format(
                    key_type=key_type_names[key_type_int],
                    key_id=key_id
                ))
                pub_keys_count -= 1
        else:
            self.__ui.print_error("TrustList storage not exists at path: {}".format(trust_list_folder))

    def __exit(self):
        sys.exit(0)

    def run_utility(self):
        choice = self.__ui.choose_from_list(self.__utility_list, 'Please enter option number: ')
        cleaned_utility_list = list(filter(lambda x: x != ["---"], self.__utility_list))
        if self._context.skip_confirm:
            user_choice = "Y"
        else:
            user_choice = str(
                self.__ui.get_user_input(
                    "Are you sure you want to choose [{}] [y/n]: ".format(cleaned_utility_list[choice][0]),
                    input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                    input_checker_msg="Allowed answers [y/n]. Please try again: ",
                    empty_allow=False
                )
            ).upper()
        if user_choice == "N":
            self.run_utility()
        else:
            cleaned_utility_list[choice][1]()

    # ############### Utility Functions ################ #

    def __get_recovery_pub_keys(self, suppress_db_warning=False):
        pub_keys = [None] * 2
        upper_keys = list(self.__upper_level_pub_keys.get_all_data(suppress_db_warning=suppress_db_warning).values())
        for key in upper_keys:
            if key["type"] == "recovery":
                if str(key["comment"]) == '1':
                    pub_keys[0] = key["key"]
                if str(key["comment"]) == '2':
                    pub_keys[1] = key["key"]
        return pub_keys

    def __dongle_unplug_and_mark(self, device_serial, key_type):

        if self._context.no_dongles:
            return

        def dongle_unplugged(dongle_serial):
            user_choice_unplugged = self.__ui.get_user_input(
                "Device unplugged? [y/n]: ",
                input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
                input_checker_msg="Allowed answers [y/n]. Please try again: ",
                empty_allow=False
            ).upper()
            if user_choice_unplugged == "Y":
                ops_status = self.__atmel.list_devices()
                if not ops_status[0]:
                    self.__logger.error("operation failed: {}".format(ops_status[1]))
                    sys.exit(ops_status[1])
                device_list = self.__dongle_chooser.list_devices_info_db(ops_status[1])
                del ops_status

                if any(filter(lambda x: True if x["device_serial"] == dongle_serial else False, device_list)):
                    self.__ui.print_warning("Device was not unplugged! Please unplug and label it!")
                    dongle_unplugged(dongle_serial)
                return
            else:
                self.__ui.print_warning("You must unplug and label the dongle!!!")
                dongle_unplugged(dongle_serial)

        self.__ui.print_message(
            "Please unplug the dongle with [{key_type}] Key and label it"
            "After the dongle was labeled, you may continue".format(key_type=key_type)
        )
        dongle_unplugged(device_serial)
        user_choice_continue = self.__ui.get_user_input(
            "Continue? [y/n]: ",
            input_checker_callback=self.__ui.InputCheckers.yes_no_checker,
            input_checker_msg="Allowed answers [y/n]. Please try again: ",
            empty_allow=False
        ).upper()
        if user_choice_continue == "Y":
            return
        else:
            self.run_utility()

    @property
    def __virgil_exporter_keys(self):
        if not self._virgil_exporter_keys:
            self._virgil_exporter_keys = dict()
            secure_transfer_private_key_path = os.path.join(self._context.secure_transfer_keys_path, "private.key")
            if not os.path.exists(secure_transfer_private_key_path):
                self.__logger.error("SecureTransfer Private Keys were not found at {}".format(
                    secure_transfer_private_key_path
                ))
                sys.exit(
                    "[FATAL]: Can't find SecureTransfer Private Key at {}".format(secure_transfer_private_key_path)
                )
            self._virgil_exporter_keys["private"] = open(
                secure_transfer_private_key_path,
                "rb"
            ).read()
            secure_transfer_public_key_path = os.path.join(self._context.secure_transfer_keys_path, "public.key")
            if not os.path.exists(secure_transfer_private_key_path):
                self.__logger.error("SecureTransfer Private Keys were not found at {}".format(
                    secure_transfer_private_key_path
                ))
                sys.exit(
                    "[FATAL]: Can't find SecureTransfer Public Key at {}".format(secure_transfer_private_key_path)
                )
            self._virgil_exporter_keys["public"] = open(
                secure_transfer_public_key_path,
                "rb"
            ).read()
            self._virgil_exporter_keys["password"] = self._context.secure_transfer_password
        return self._virgil_exporter_keys

    @property
    def __utility_list(self):
        if not self._utility_list:
            self._utility_list = []
            self._utility_list.extend([
                ["Initial Generation ({0} Recovery, {0} Auth, {0} TL Service, {0} Firmware, 1 Factory)"
                    .format(self.__upper_level_keys_count), self.__generate_initial_keys],
                ["---"],
                ["Generate Recovery Key ({})".format(self.__upper_level_keys_count), self.__generate_recovery_by_count],
                ["Revive the Recovery Key from the RestorePaper", self.__revive_recovery_key],
                ["---"],
                ["Generate Auth Key ({})".format(self.__upper_level_keys_count), self.__generate_auth_by_count],
                ["Generate AuthInternal Key", self.__generate_auth_internal_key],
                ["Revive the Auth Key from the RestorePaper", self.__revive_auth_key],
                ["---"],
                ["Generate TrustList Service Key ({})".format(self.__upper_level_keys_count),
                 self.__generate_tl_key_by_count],
                ["Revive the TrustList Service Key from the RestorePaper", self.__revive_tl_service_key],
                ["---"],
                ["Generate Factory Key", self.__generate_factory_key],
                ["Create a new Factory dongle from the existing Key", self.__re_generate_factory_key],
                ["Delete Factory Key", self.__delete_factory_key],
                ["---"],
                ["Generate Firmware Key ({})".format(self.__upper_level_keys_count), self.__generate_firmware_by_count],
                ["Generate FirmwareInternal Key", self.__generate_firmware_internal_key],
                ["Revive the Firmware Key from the RestorePaper", self.__revive_firmware_key],
                ["---"],
                ["Generate TrustList", self.__generate_trust_list],
                ["---"],
                ["Create a provision for SDMPD and Sandbox", self.__generate_provision],
                ["---"],
                ["Print all Public Keys from db's", self.__print_all_pub_keys_db],
                ["Add Public Key to db (Factory)", self.__manual_add_public_key],
                ["Dump upper level Public Keys", self.__dump_upper_level_pub_keys],
                ["Restore UpperLevelKeys db from dongles", self.__restore_upper_level_db_from_keys],
                ["Import TrustList to db", self.__import_trust_list_to_db],
                ["---"]
            ])
            if self._context.no_dongles:
                self._utility_list.append(
                    ["Export Private Keys", self.__get_all_private_keys]
                )
            else:
                self._utility_list.extend([
                    ["Export Internal Private Keys", self.__get_internal_private_keys],
                    ["---"]
                ])
            self._utility_list.append(["Exit", self.__exit])

        return self._utility_list

    def __update_signature_in_db(self):
        self.__ui.print_message("Update Key signature in db...")
        ops_status = self.__atmel.list_devices()
        if not ops_status[0]:
            self.__logger.error("operation failed: {}".format(ops_status[1]))
            self.__ui.print_error(ops_status[1])
            return
        device_list = ops_status[1]
        del ops_status

        if device_list:
            first_device = device_list[0]
        else:
            self.__ui.print_error("Can't find any dongles")
            return

        ops_status = self.__atmel.get_public_key(first_device)
        if not ops_status[0]:
            self.__logger.error("operation failed: {}".format(ops_status[1]))
            self.__ui.print_error(ops_status[1])
            return
        pub_key = ops_status[1]
        del ops_status

        key_id = str(CRCCCITT().calculate(base64.b64decode(pub_key)))

        if key_id in self.__upper_level_pub_keys.get_keys():
            ops_status = self.__atmel.get_signature(first_device)
            if not ops_status[0]:
                self.__ui.print_error("Can't get Key signature from dongle")
                return
            self.__upper_level_pub_keys.save(key_id, ops_status[1])
        del ops_status

        self.__ui.print_message("Signature updated")

    def __save_key(self, key_id, key, file_path, signer_key_id=None, signature=None):
        byte_buffer = io.BytesIO()
        byte_buffer.write(int(key_id).to_bytes(2, byteorder='little', signed=False))
        byte_buffer.write(base64.b64decode(key))
        if signer_key_id and signature:
            byte_buffer.write(int(signer_key_id).to_bytes(2, byteorder='little', signed=False))
            byte_buffer.write(b64_to_bytes(signature))
        else:
            byte_buffer.write(bytes(66))
        open(file_path, "wb").write(bytes(byte_buffer.getvalue()))

    def __save_private_key(self, key, file_path):
        byte_buffer = io.BytesIO()
        byte_buffer.write(b64_to_bytes(key))
        open(file_path, "wb").write(bytes(byte_buffer.getvalue()))

    def __get_private_keys(self, storage):
        private_dir = os.path.join(self.__key_storage_path, "private")
        if not os.path.exists(private_dir):
            os.mkdir(private_dir)

        storage_keys = storage.get_keys()
        if not storage_keys:
            self.__logger.error("Db {} empty or not exist".format(storage.storage_path))
            self.__ui.print_warning("Db {} empty or not exist".format(storage.storage_path))
            return
        for key_id in storage.get_keys():
            key_data = storage.get_value(key_id)
            self.__save_private_key(
                key_data["key"],
                os.path.join(private_dir, key_data["type"] + "_" + key_id + "_" + str(key_data["comment"]) + ".key")
            )

    def __get_all_private_keys(self):
        self.__ui.print_message("Exporting Private Keys...")
        self.__get_private_keys(self.__factory_priv_keys)
        self.__get_private_keys(self.__firmware_priv_keys)
        self.__get_private_keys(self.__auth_private_keys)
        self.__get_private_keys(self.__recovery_private_keys)
        self.__get_private_keys(self.__trust_list_service_private_keys)
        self.__get_private_keys(self.__internal_private_keys)
        self.__ui.print_message("Export finished")

    def __get_internal_private_keys(self):
        self.__ui.print_message("Exporting Private Keys...")
        self.__get_private_keys(self.__internal_private_keys)
        self.__ui.print_message("Export finished")

    def __check_db_path(self):
        db_path = os.path.join(self.__key_storage_path, "db")
        if not os.path.exists(db_path):
            os.makedirs(db_path)
