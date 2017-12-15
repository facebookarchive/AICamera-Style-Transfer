if(USE_MOBILE_OPENGL AND (ANDROID OR IOS))
  add_subdirectory(core)
  add_subdirectory(operators)

  if (ANDROID)
    add_subdirectory(android)
    add_subdirectory(test)
  endif()

  if (IOS)
    add_subdirectory(ios)
  endif()
endif()

set(Caffe2_CPU_SRCS ${Caffe2_CPU_SRCS} PARENT_SCOPE)
