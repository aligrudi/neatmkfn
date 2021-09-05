cmake_minimum_required(VERSION 3.0)
project(neatmkfn C)
add_executable(mkfn mkfn.c trfn.c sbuf.c tab.c afm.c otf.c)
install(TARGETS mkfn)
