#pragma once
#include <array>
#include <filesystem>
#include <vector>
#include "vec.hpp"
namespace jyd {
    struct FaceVertex {
        std::size_t positionIndex = 0;
        int texcoordIndex = -1;
        int normalIndex = -1;
    };

    using Face = std::array<FaceVertex, 3>;

    class Model {
    public:
        std::vector<std::vector<float>> verts = {};
        std::vector<std::vector<float>> vnormals = {};
        std::vector<Face> facet = {};
        std::vector<std::vector<float>> vtexcoords = {};
    public:
        explicit Model(const std::filesystem::path& filename);
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
