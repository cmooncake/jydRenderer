#include "model.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

std::size_t resolveIndex(
    int rawIndex,
    std::size_t itemCount,
    std::size_t lineNumber,
    const char* kind) {
    if (rawIndex == 0) {
        throw std::runtime_error(
            "OBJ line " + std::to_string(lineNumber) +
            ": " + kind + " index cannot be zero");
    }

    const long long resolved = rawIndex > 0
        ? static_cast<long long>(rawIndex) - 1
        : static_cast<long long>(itemCount) + rawIndex;
    if (resolved < 0 || resolved >= static_cast<long long>(itemCount)) {
        throw std::runtime_error(
            "OBJ line " + std::to_string(lineNumber) +
            ": " + kind + " index is out of range");
    }
    return static_cast<std::size_t>(resolved);
}

jyd::FaceVertex parseFaceVertex(
    const std::string& token,
    std::size_t vertexCount,
    std::size_t normalCount,
    std::size_t lineNumber) {
    const std::size_t firstSlash = token.find('/');
    const std::size_t secondSlash = firstSlash == std::string::npos
        ? std::string::npos
        : token.find('/', firstSlash + 1);

    const std::string positionPart = token.substr(0, firstSlash);
    if (positionPart.empty()) {
        throw std::runtime_error(
            "OBJ line " + std::to_string(lineNumber) +
            ": face vertex has no position index");
    }

    jyd::FaceVertex result;
    try {
        result.positionIndex = resolveIndex(
            std::stoi(positionPart), vertexCount, lineNumber, "vertex");

        if (secondSlash != std::string::npos && secondSlash + 1 < token.size()) {
            const std::string normalPart = token.substr(secondSlash + 1);
            result.normalIndex = static_cast<int>(resolveIndex(
                std::stoi(normalPart), normalCount, lineNumber, "normal"));
        }
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(
            "OBJ line " + std::to_string(lineNumber) +
            ": face contains an invalid index");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(
            "OBJ line " + std::to_string(lineNumber) +
            ": face index is too large");
    }
    return result;
}

} // namespace

namespace jyd
{
    Model::Model(const std::filesystem::path& filename)
        : position_(0), scale_(1), rotation_(0) {
        std::ifstream in(filename);
        if (!in) {
            throw std::runtime_error("Cannot open model file: " + filename.string());
        }

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            std::istringstream iss(line);
            if (!line.compare(0, 2, "v ")) {
                char prefix = 0;
                iss >> prefix;
                std::vector<float> v = { 0,0,0 };
                if (!(iss >> v[0] >> v[1] >> v[2])) {
                    throw std::runtime_error(
                        "OBJ line " + std::to_string(lineNumber) +
                        ": invalid vertex");
                }
                verts.push_back(v);
            }
            else if (!line.compare(0, 2, "f ")) {
                char prefix = 0;
                iss >> prefix;
                std::vector<FaceVertex> polygon;
                std::string token;
                while (iss >> token) {
                    polygon.push_back(parseFaceVertex(
                        token, verts.size(), vnormals.size(), lineNumber));
                }
                if (polygon.size() < 3) {
                    throw std::runtime_error(
                        "OBJ line " + std::to_string(lineNumber) +
                        ": face must contain at least three vertices");
                }
                for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                    facet.push_back({polygon[0], polygon[i], polygon[i + 1]});
                }
            }
            else if (!line.compare(0, 3, "vn ")) {
                std::string prefix;
                iss >> prefix;
                std::vector<float> vn = { 0,0,0 };
                if (!(iss >> vn[0] >> vn[1] >> vn[2])) {
                    throw std::runtime_error(
                        "OBJ line " + std::to_string(lineNumber) +
                        ": invalid normal");
                }
                vnormals.push_back(vn);
            }
        }

        if (verts.empty() || facet.empty()) {
            throw std::runtime_error(
                "The selected OBJ does not contain renderable faces: " +
                filename.string());
        }
    }
}
