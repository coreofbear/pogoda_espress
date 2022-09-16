# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/mikhail/esp/esp-idf/components/bootloader/subproject"
  "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader"
  "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix"
  "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix/tmp"
  "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix/src/bootloader-stamp"
  "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix/src"
  "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/mikhail/projects/roadmap/pogoda_espress/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
