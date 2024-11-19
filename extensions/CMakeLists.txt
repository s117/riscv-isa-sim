cmake_minimum_required(VERSION 3.3)

file(GLOB extension_project_folders LIST_DIRECTORIES true *)

foreach (item ${extension_project_folders})
    if (IS_DIRECTORY ${item})
        add_subdirectory(${item})
        message("Added extension project: ${item}")
    endif ()
endforeach ()