#!/bin/bash

SCRIPT_PATH="$(cd `dirname $0` && pwd)"

DEFAULT_SOURCE_PATH="${SCRIPT_PATH}/esp32-flsher-sources"
SOURCE_PATH=""

ERASE_FLASH=""

DEFAULT_GIT_BRANCH="feature/msgr-service"
GIT_BRANCH=""

DEFAULT_PIO_PROJECT="initializer"
PIO_PROJECT=""

CLEAN_SOURCES=""

ESP_WIFI_SSID=""
ESP_WIFI_PASS=""

SKIP_ASK_ANY_KEY=""

ESP_ENVIROMENT=""

DEBUG_OUT=""

#**********************************************************************************
print_help() {
  echo "$(basename $0) - virgil exp32 iotkit flasher."
  echo ""  
  echo "    -b <branch>            - iotkit GIT repository branch"
  echo "    -s <path to source>    - exiting source tree path"
  echo "    -p <project to build>  - project for building and flashing"  
  echo "    -c                     - clean source tree"    
  echo "    -w <WIFI SSID>         - WIFI ESSID"    
  echo "    -k <WIFI KEY>          - WIFI KEY"      
  echo "    -e                     - build platformio enviroment"    
  echo "    -t                     - erase flash"      
  echo "    -x                     - Skip ask any key for flashing"  
  echo "    -d                     - Debug output"    
  echo "    -h                     - print help"    

}

#**********************************************************************************
echo "Copyright (C) 2015-2020 Virgil Security, Inc."
echo
while [ -n "$1" ]
 do
  case "$1" in
    -b) GIT_BRANCH="$2"
        shift 
        ;;
    -s) SOURCE_PATH="$2"
        shift 
        ;;        
    -p) PIO_PROJECT="$2"
        shift 
        ;;                
    -w) ESP_WIFI_SSID="$2"
        shift 
        ;;              
    -k) ESP_WIFI_PASS="$2"
        shift 
        ;;              
    -e) ESP_ENVIROMENT="$2"
        shift 
        ;;                      
    -c) CLEAN_SOURCES="1"
        ;;        
    -t) ERASE_FLASH="1"
        ;;
    -x) SKIP_ASK_ANY_KEY="1"
        ;;        
    -d) DEBUG_OUT="1"
        ;;                
    -h) print_help
        exit 0
        ;;        
     *) echo -e "Error [$1] is not an option. \n-help for information";;
  esac
  shift
done

#**********************************************************************************
print_stage() {
  echo -n "### ${1}"
}

#**********************************************************************************
print_OK() {
  echo -e "OK ${1}"
}

#**********************************************************************************
print_ERROR() {
  echo -e "ERROR ${1}"
}

#**********************************************************************************
check_depends() {
  local RET_RES=0
  which python3 > /dev/null 2>&1
  if [ "$?" != "0" ]; then
    RET_RES="1"
    echo "Python V3 not installed"
    echo "Please installing python3. Visit to https://www.python.org/about/gettingstarted/"
    echo    
  fi 

  which pip3 > /dev/null 2>&1
  if [ "$?" != "0" ]; then
    RET_RES="1"  
    echo "Python PIP3 not installed"
    echo "Please installing python3 pip. Visit to https://pip.pypa.io/en/stable/installing/"
    echo    
  fi   

  which git > /dev/null 2>&1
  if [ "$?" != "0" ]; then
    RET_RES="1"  
    echo "GIT not installed"
    echo "Please installing git. Visit to https://git-scm.com/downloads"
    echo    
  fi     

  which pio > /dev/null 2>&1
  if [ "$?" != "0" ]; then
    RET_RES="1"  
    echo "Platformio tool not installed"
    echo "Please installing platformio. Use pip3 install platformio"
    echo
  fi       
  return ${RET_RES}
}

#**********************************************************************************
get_iotkit(){
    local TMP_STR=""
    local WORK_BRANCH="${GIT_BRANCH:-$DEFAULT_GIT_BRANCH}"
    if [ -d ${DEFAULT_SOURCE_PATH}/virgil-iotkit ]; then
      echo "SKIPPED EXIST"
      return 0
    fi
    mkdir -p ${DEFAULT_SOURCE_PATH}  2>&1
    TMP_STR=$(git clone --recurse-submodules  -b ${WORK_BRANCH} https://github.com/VirgilSecurity/virgil-iotkit.git ${DEFAULT_SOURCE_PATH}/virgil-iotkit 2>&1)
    if [ "$?" != "0" ]; then
     echo "${TMP_STR}"
     return 1              
    fi
    echo "DOWNLOADED BRANCH:[${WORK_BRANCH}]"
    return 0
}

#**********************************************************************************
iotkit_checkout(){
    local TMP_STR=""
    WORK_DIR="${SOURCE_PATH:-$DEFAULT_SOURCE_PATH}/virgil-iotkit"
    
    pushd ${WORK_DIR} > /dev/null 2>&1
    TMP_STR=$(git fetch 2>&1)
    if [ "$?" != "0" ]; then
     echo "${TMP_STR}"
     return 1              
    fi
    TMP_STR+=$(git checkout ${GIT_BRANCH} 2>&1)
    if [ "$?" != "0" ]; then
     echo "${TMP_STR}"
     return 1              
    fi
    popd > /dev/null 2>&1
    echo "CHECLOUT BRANCH:[${GIT_BRANCH}]"
    return 0
}

#**********************************************************************************
pio_prep_wifi(){
  WORK_PROJECT="${PIO_PROJECT:-$DEFAULT_PIO_PROJECT}"
  WORK_DIR="${SOURCE_PATH:-$DEFAULT_SOURCE_PATH}/virgil-iotkit/demo/esp32/${WORK_PROJECT}"
  pushd ${WORK_DIR} > /dev/null 2>&1
cat <<EOF >wifi_data.ini
[common]
 src_build_flags = -DESP_WIFI_SSID=\"${ESP_WIFI_SSID}\" -DESP_WIFI_PASS=\"${ESP_WIFI_PASS}\"
EOF
  popd > /dev/null 2>&1
}

#**********************************************************************************
pio_clean(){
  local LOCAL_RES=""
  local WORK_ENV=""
  WORK_PROJECT="${PIO_PROJECT:-$DEFAULT_PIO_PROJECT}"
  WORK_DIR="${SOURCE_PATH:-$DEFAULT_SOURCE_PATH}/virgil-iotkit/demo/esp32/${WORK_PROJECT}"
  [ "${ESP_ENVIROMENT}" != "" ] && WORK_ENV="-e $ESP_ENVIROMENT"
  pushd ${WORK_DIR} > /dev/null 2>&1
    pio  run ${WORK_ENV} --target clean 2>&1
    LOCAL_RES="${?}"  
  popd > /dev/null 2>&1
  [ "$LOCAL_RES" != "0" ] && return 1  
  return 0
}

#**********************************************************************************
pio_build(){
  local LOCAL_RES=""
  local WORK_ENV=""
  WORK_PROJECT="${PIO_PROJECT:-$DEFAULT_PIO_PROJECT}"
  WORK_DIR="${SOURCE_PATH:-$DEFAULT_SOURCE_PATH}/virgil-iotkit/demo/esp32/${WORK_PROJECT}"
  [ "${ESP_ENVIROMENT}" != "" ] && WORK_ENV="-e ${ESP_ENVIROMENT}"
  pushd ${WORK_DIR} > /dev/null 2>&1
    echo "pio run ${WORK_ENV}"
    pio run ${WORK_ENV} 2>&1
    LOCAL_RES="${?}"
  popd > /dev/null 2>&1
  [ "$LOCAL_RES" != "0" ] && return 1  
  return 0
}


#**********************************************************************************
pio_erase(){
  local LOCAL_RES=""
  local WORK_ENV=""  
  WORK_PROJECT="${PIO_PROJECT:-$DEFAULT_PIO_PROJECT}"
  WORK_DIR="${SOURCE_PATH:-$DEFAULT_SOURCE_PATH}/virgil-iotkit/demo/esp32/${WORK_PROJECT}"
  [ "${ESP_ENVIROMENT}" != "" ] && WORK_ENV="-e $ESP_ENVIROMENT"  
  pushd ${WORK_DIR} > /dev/null 2>&1
    pio run ${WORK_ENV} --target erase  2>&1
    LOCAL_RES="${?}"
  popd > /dev/null 2>&1
  [ "$LOCAL_RES" != "0" ] && return 1  
  return 0
}

#**********************************************************************************
pio_flashing(){
  local LOCAL_RES=""
  local WORK_ENV=""
  WORK_PROJECT="${PIO_PROJECT:-$DEFAULT_PIO_PROJECT}"
  WORK_DIR="${SOURCE_PATH:-$DEFAULT_SOURCE_PATH}/virgil-iotkit/demo/esp32/${WORK_PROJECT}"
  [ "${ESP_ENVIROMENT}" != "" ] && WORK_ENV="-e $ESP_ENVIROMENT"    
  pushd ${WORK_DIR} > /dev/null 2>&1
    pio run ${WORK_ENV} --target upload 2>&1
    LOCAL_RES="${?}"
  popd > /dev/null 2>&1
  [ "$LOCAL_RES" != "0" ] && return 1  
  return 0
}

#**********************************************************************************
# Check depends
print_stage "Checking dependencies ... "
RET_STR=$(check_depends)
if [ "${?}" != "0" ]; then
  print_ERROR "\n${RET_STR}"
  exit 1
else  
  print_OK
  [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"
fi

# Get sources
if [ "$SOURCE_PATH" == "" ]; then
  print_stage "Get iotkit sorces. ... "
  RET_STR=$(get_iotkit)
  if [ "${?}" != "0" ]; then
    print_ERROR "\n${RET_STR}"
    exit 1
  else  
    print_OK "$RET_STR"
    [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"    
  fi
else  
  print_stage "Get IOTKIT sources spipped. Custom directory Indicated [${SOURCE_PATH}] ... "
  if [ -d ${SOURCE_PATH} ]; then
    print_OK
    [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"    
  else
    print_ERROR
    echo "Derectory not exist"
  fi
fi

# Checkout
if [ "${GIT_BRANCH}" != "" ]; then
  print_stage "Checkout sources ... "
  RET_STR=$(iotkit_checkout)
  if [ "${?}" != "0" ]; then
    print_ERROR "\n${RET_STR}"
    exit 1
  else  
    print_OK "${RET_STR}"
    [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"
  fi
fi

# Clean sources
if [ "${CLEAN_SOURCES}" != "" ]; then
  print_stage "Clean source tree ... "
  RET_STR=$(pio_clean)
  if [ "${?}" != "0" ]; then
    print_ERROR "\n${RET_STR}"
    exit 1
  else  
    print_OK
    [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"
  fi
fi

# Write configuration for wifi credentials
print_stage "Prepare WiFi credentioals ... "
RET_STR=$(pio_prep_wifi)
print_OK
[ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"

# Building firmware
print_stage "Building firmware ... "
RET_STR=$(pio_build)
if [ "${?}" != "0" ]; then
  print_ERROR "\n${RET_STR}"
  exit 1
else  
  print_OK
  [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"  
fi

# Press any key
if [ "${SKIP_ASK_ANY_KEY}" != "1" ]; then
echo "For flashing recomended press the BOOT button"
echo "then EN, release the BOOT button, release EN."
read -n 1 -s -r -p "Press any key to continue"
echo
fi

# Erasing flash
if [ "${ERASE_FLASH}" == "1" ]; then
  print_stage "Erasing esp32 flash ... "
  RET_STR=$(pio_erase)
  if [ "${?}" != "0" ]; then
    print_ERROR "\n${RET_STR}"
    exit 1
  else  
    print_OK
    [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"
  fi
fi

# Flashing firmware
print_stage "Flashing firmware ... "
RET_STR=$(pio_flashing)
if [ "${?}" != "0" ]; then
  print_ERROR "\n${RET_STR}"
  exit 1
else  
  print_OK
  [ "${DEBUG_OUT}" == "1" ] && echo "${RET_STR}"
fi

