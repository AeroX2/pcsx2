add_library(manymouse STATIC
	linux_evdev.c
	macosx_hidmanager.c
	macosx_hidutilities.c
	manymouse.c
	manymouse.h
	windows_wminput.c
	x11_xinput2.c
)

target_include_directories(manymouse PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
	target_link_libraries(manymouse PRIVATE ${CMAKE_DL_LIBS})
	if(NOT X11_API OR NOT X11_Xi_FOUND)
		target_compile_definitions(manymouse PRIVATE SUPPORT_XINPUT2=0)
	endif()
elseif(APPLE)
	target_link_libraries(manymouse PRIVATE "-framework Carbon" "-framework IOKit")
endif()

