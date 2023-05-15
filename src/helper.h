#include "Halide.h"
#include <vector>
#include <string>

int read_args(int argc, char** argv, int& schedule, bool& only_memory, bool& front, std::string& name){
    only_memory = false;
    std::string front_s = "front";
    std::string only_memory_s = "mem";
    front = false;
    if(argc == 1){
        printf("Need output name\n");
        return 1;
    }
    name = argv[1];

    if(argc == 2){
        schedule = 0;
    } else if(argc == 3 || argc == 4){
    if(front_s.compare(argv[2]) == 0){
        front = true;
    } else {
        schedule = std::stoi(argv[2]);
    }
    if(argc == 4) {
        if(only_memory_s.compare(argv[3]) == 0){
            only_memory = true;
        } else {
            printf("Invallid argument\n");
            return 1;
        }
    }
    }
    if((argc > 4 || schedule < 0 || schedule >3) && !front){
        printf("Invallid argument\n");
        return 1;
    }

    return 0;
}

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p){
    int stride = 1;
    for(int i = 0; i < dims.size(); i++){
        p.dim(i).set_bounds(std::get<0>(dims[i]), std::get<1>(dims[i]));
        p.dim(i).set_stride(stride);
        stride *= std::get<1>(dims[i]);
    }
}

Halide::Target standard_target() {
    Halide::Target target = Halide::Target();
    Halide::Target new_target = target
        .with_feature(Halide::Target::NoAsserts)
        .with_feature(Halide::Target::NoBoundsQuery)
        ;
    return new_target;
}