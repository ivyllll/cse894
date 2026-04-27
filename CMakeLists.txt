cmake_minimum_required(VERSION 3.14)
project(lockfree_reclamation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optimization flags.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# std::atomic_load on shared_ptr is deprecated in C++20; silence the warning.
add_compile_options(-Wno-deprecated-declarations)

# Note: ThreadSanitizer is not enabled by default on macOS clang.
# Add -fsanitize=thread manually via the bench_tsan target below.

include_directories(include)

add_executable(bench src/main.cpp)

# pthread is required on Linux; harmless on macOS.
find_package(Threads REQUIRED)
target_link_libraries(bench PRIVATE Threads::Threads)

# Optional: ThreadSanitizer build (for diagnosing concurrency bugs).
add_executable(bench_tsan EXCLUDE_FROM_ALL src/main.cpp)
target_compile_options(bench_tsan PRIVATE -fsanitize=thread -g -O1)
target_link_options(bench_tsan PRIVATE -fsanitize=thread)
target_link_libraries(bench_tsan PRIVATE Threads::Threads)

# Optional: AddressSanitizer build (for diagnosing use-after-free).
add_executable(bench_asan EXCLUDE_FROM_ALL src/main.cpp)
target_compile_options(bench_asan PRIVATE -fsanitize=address -g -O1)
target_link_options(bench_asan PRIVATE -fsanitize=address)
target_link_libraries(bench_asan PRIVATE Threads::Threads)
