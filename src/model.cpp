#include "model.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace jyd
{
    Model::Model(const std::string filename) : position_(0), scale_(1), rotation_(0){
        std::ifstream in;
        in.open(filename, std::ifstream::in);
        if (in.fail()) return;
        std::string line;
        while (!in.eof()) {
            std::getline(in, line);
            std::istringstream iss(line.c_str());
            char trash;
            if (!line.compare(0, 2, "v ")) {
                iss >> trash;
                std::vector<float> v = { 0,0,0 };
                for (int i : {0, 1, 2}) iss >> v[i];
                verts.push_back(v);
            }
            else if (!line.compare(0, 2, "f ")) {
                int f, t, n, cnt = 0;
                iss >> trash;
                std::vector<int> face = {};
                while (iss >> f >> trash >> t >> trash >> n) {
                    face.push_back(--f);
                    cnt++;
                }
                if (3 != cnt) {
                    std::cerr << "Error: the obj file is supposed to be triangulated" << std::endl;
                    return;
                }
                facet.push_back(face);
            }
        }
       
    }


}