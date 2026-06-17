#pragma once
#include <vector>
#include <string>
#include "vec.hpp"
namespace jyd {
    class Model {
    public:
        std::vector<std::vector<float>> verts = {};
        std::vector<std::vector<int>> facet = {};
    public:
        Model(const std::string filename);
        inline vec3& position() { return position_; }
        inline vec3& scale() { return scale_; }
        inline vec3& rotation() { return rotation_; }

        inline void setPosition(const vec3& pos) { position_ = pos; }
        inline void setScale(const vec3& sca) { scale_ = sca; }
        inline void setRotation(const vec3& rot) { rotation_ = rot; }

    private:
        vec3 position_;
        vec3 scale_;
        vec3 rotation_;
    };

}