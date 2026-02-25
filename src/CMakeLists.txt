#***********************************************************
#***********************************************************

set(SRC_LIST	bitmap.c
				coroutine.c
				hashmap.c
				log.c
				mcache.c
				mempool.c
				semaphore.c
				stimer.c
				task.c
				threadpool.c
				cmdline.cpp
				statis/costat.cpp
				statis/mcstat.cpp
				statis/tpstat.cpp)

#***********************************************************
#***********************************************************

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/statis)

include_directories(${SECUREC_DIR}/include)
include_directories(${ZLOG_DIR}/include)

link_directories(${ZLOG_DIR}/lib)
link_directories(${SECUREC_DIR}/lib)

#***********************************************************
#***********************************************************

set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
add_library(infra SHARED ${SRC_LIST})
target_link_libraries(infra zlog securec pthread)