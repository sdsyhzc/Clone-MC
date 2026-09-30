#pragma once
#include <glad/glad.h>
// #include <stb_image.h>
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct BlockFaceTextures {
    std::string top, bottom, north, south, west, east;
};

// 加载纹理数组，返回纹理 ID，同时填出 layerMap（名字 -> 层索引）
inline unsigned int loadTextureArray(const std::string& dir,
                                     const std::vector<std::string>& names,
                                     std::map<std::string, int>& layerMap) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);

    int w = 0, h = 0;
    std::vector<unsigned char*> images;
    stbi_set_flip_vertically_on_load(false);
    for (size_t i = 0; i < names.size(); i++) {
        std::string path = dir + "/" + names[i] + ".png";
        int tw, th, tch;
        unsigned char* data = stbi_load(path.c_str(), &tw, &th, &tch, 4);
        if (!data) { std::cerr << "加载失败: " << path << std::endl; continue; }
        if (w == 0) { w = tw; h = th; }
        images.push_back(data);
        layerMap[names[i]] = (int)i;
    }

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, (GLsizei)images.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    for (size_t i = 0; i < images.size(); i++) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint)i, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, images[i]);
        stbi_image_free(images[i]);
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return tex;
}