#include<iostream>
#include <nlohmann/json.hpp>

int main(){
    nlohmann::json obs={{"screen", "catalog"}, {"goal", "blue-mug x2"}};
    std::cout<<obs.dump(4)<<"\n";
}

