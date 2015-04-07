project(kopete-webqq)
set(LWQQ_VERSION_MAJOR "0")
set(LWQQ_VERSION_MINOR "4")
set(LWQQ_VERSION_PATCH "0")


set(CMAKE_MODULE_PATH 
    ${CMAKE_SOURCE_DIR}/cmake/modules
    ${CMAKE_SOURCE_DIR}/liblwqq/cmake
    ${CMAKE_MODULE_PATH}
)

#add_subdirectory( liblwqq )

#if(CMAKE_COMPILER_IS_GNUCXX)
#	set(CMAKE_CXX_FLAGS "-fpermissive")
#endif(CMAKE_COMPILER_IS_GNUCXX) 

find_package(KDE4 REQUIRED)

include(KDE4Defaults)
include(MacroLibrary)

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckPrototypeExists)
include(CheckTypeSize)
add_definitions( -DKDE_DEFAULT_DEBUG_AREA=14210 )
find_package(PkgConfig REQUIRED)
pkg_check_modules(LWQQ REQUIRED lwqq>=${LWQQ_VERSION_MAJOR}.${LWQQ_VERSION_MINOR}.${LWQQ_VERSION_PATCH})
set(SHARE_DIR "${CMAKE_INSTALL_PREFIX}/share")
set(CMAKE_CXX_FLAGS "-fpermissive")


set(CMAKE_REQUIRED_DEFINITIONS ${_KDE4_PLATFORM_DEFINITIONS})

if (WIN32)
	set(DISABLE_VIDEOSUPPORT ON)
else (WIN32)
	option(DISABLE_VIDEOSUPPORT "Build Kopete without video-support" ON)
endif (WIN32)
if (DISABLE_VIDEOSUPPORT)
	add_definitions(-DVIDEOSUPPORT_DISABLED)
endif (DISABLE_VIDEOSUPPORT)

if(WIN32)
   set(CMAKE_REQUIRED_LIBRARIES ${KDEWIN32_LIBRARIES})
   set(CMAKE_REQUIRED_INCLUDES  ${KDEWIN32_INCLUDES})
endif(WIN32)
add_definitions(${QT_DEFINITIONS} ${QT_QTDBUS_DEFINITIONS} ${KDE4_DEFINITIONS})
add_definitions(-DQT_USE_FAST_CONCATENATION -DQT_USE_FAST_OPERATOR_PLUS)
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${KDE4_INCLUDES})


set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH} )


find_package(Kopete REQUIRED)


#KDE4_NO_ENABLE_FINAL(kopete)


set(datadir ${SHARE_DIR}/lwqq)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h )

# For config-kopete.h
#include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

link_directories( 
${KOPETE_BINARY_DIR}/lib
)



add_subdirectory( icons ) 
add_subdirectory( emoticons ) 


include_directories( 
${KOPETE_INCLUDE_DIRS} 
${KOPETE_INCLUDE_DIRS}/ui/
${CMAKE_CURRENT_SOURCE_DIR}/ui/ 
${LWQQ_INCLUDE_DIRS}
)



########### next target ###############

set(kopete_qq_PART_SRCS 
   qqprotocol.cpp 
   qq_types.cpp
   qqcontact.cpp 
   qqaccount.cpp 
   qqaddcontactpage.cpp 
   qqeditaccountwidget.cpp 
   qqbridgecallback.cpp
   qqloginverifywidget.cpp
   qquserinfoform.cpp
   qqchatsession.cpp
   qqdiscuchatsession.cpp
   qqgroupchatsession.cpp
   qqshowgetinfo.cpp
   translate.cpp
   trex.cpp
   ui/qqwebcamdialog.cpp 
)



kde4_add_ui_files(kopete_qq_PART_SRCS
qqaddui.ui
)

kde4_add_ui_files(kopete_qq_PART_SRCS
qqaccountpreferences.ui
)

kde4_add_ui_files(kopete_qq_PART_SRCS
qquserinfoform.ui
)

kde4_add_plugin(kopete_webqq ${kopete_qq_PART_SRCS})


target_link_libraries(kopete_webqq ${KDE4_KIO_LIBS} kopete  lwqq)

#Urgly!!!
set(PLUGIN_INSTALL_DIR "/usr/lib/kde4/")
set(SERVICES_INSTALL_DIR "/usr/share/kde4/services/")
 
install(TARGETS kopete_webqq  DESTINATION ${PLUGIN_INSTALL_DIR})

########### install files ###############

install( FILES kopete_webqq.desktop  DESTINATION ${SERVICES_INSTALL_DIR})
install( FILES qqdiscuui.rc qqchatui.rc qqgroupui.rc  DESTINATION ${DATA_INSTALL_DIR}/kopete_webqq)
