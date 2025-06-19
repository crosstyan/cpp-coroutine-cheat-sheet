add_library(
    timer 
    STATIC 
        timer/app_timer.cpp)
target_include_directories(timer PUBLIC timer)
