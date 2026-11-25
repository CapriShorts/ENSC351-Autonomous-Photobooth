# Build the app, using the HAL
include_directories(include)
file(GLOB MY_SOURCES "src/*.c")
add_executable(hello_world ${MY_SOURCES})

# Make use of the HAL and grabber libraries
target_link_libraries(hello_world PRIVATE hal gpiod grabber)

# Copy executable to final location
add_custom_command(TARGET hello_world POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:hello_world>"
    "$ENV{HOME}/ensc351/public/myApps/hello_world"
  COMMENT "Copying ARM executable to public NFS directory")