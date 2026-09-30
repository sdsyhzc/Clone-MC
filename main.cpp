#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>
#include "block_load.h"
#include "perlin_noise.hpp"

const int SCREEN_W = 1280, SCREEN_H = 720;

// ===== 3D 着色器 =====
const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in float aLayer;
layout (location = 3) in vec3 aNormal;
layout (location = 4) in vec3 aTint;
uniform mat4 model, view, projection;
out vec2 TexCoord;
out float Layer;
out vec3 Normal, FragPos, Tint;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    Layer = aLayer;
    Tint = aTint;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char *fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
in float Layer;
in vec3 Normal, FragPos, Tint;
out vec4 FragColor;
uniform sampler2DArray texArray;
uniform vec3 lightPos, viewPos, lightColor;
uniform float ambientStrength;
void main() {
    vec4 texColor = texture(texArray, vec3(TexCoord, Layer));
    texColor.rgb *= Tint;
    vec3 ambientColor = mix(lightColor, vec3(0.4f, 0.4f, 0.6f), 0.7f);
    vec3 ambient = ambientStrength * ambientColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    float spec = pow(max(dot(normalize(viewPos - FragPos), reflect(-lightDir, norm)), 0.0), 32.0);
    vec3 specular = 0.5 * spec * lightColor;
    FragColor = vec4((ambient + diffuse + specular) * texColor.rgb, texColor.a);
}
)";

// ===== 破坏层着色器 =====
const char *breakVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
uniform mat4 model, view, projection;
out vec2 TexCoord;
void main() {
    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char *breakFragmentShader = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2DArray texArray;
uniform float layer;
void main() {
    vec4 c = texture(texArray, vec3(TexCoord, layer));
    if (c.a < 0.1) discard;
    FragColor = c;
}
)";

// ===== 2D 文字着色器 =====
const char *textVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
uniform mat4 projection;
out vec4 Color;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    Color = aColor;
}
)";

const char *textFragmentShader = R"(
#version 330 core
in vec4 Color;
out vec4 FragColor;
void main() {
    FragColor = Color;
}
)";

// ===== 图标着色器 =====
const char *iconVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
uniform mat4 projection;
uniform vec2 offset;
uniform float scale;
out vec2 UV;
void main() {
    gl_Position = projection * vec4(aPos * scale + offset, 0.0, 1.0);
    UV = aUV;
}
)";

const char *iconFragmentShader = R"(
#version 330 core
in vec2 UV;
out vec4 FragColor;
uniform sampler2DArray texArray;
uniform float layer;
uniform vec3 tint;
void main() {
    vec4 c = texture(texArray, vec3(UV, layer));
    if (c.a < 0.1) discard;
    FragColor = vec4(c.rgb * tint, 1.0);
}
)";

class Camera
{
public:
    glm::vec3 Position, Front, Up, Right, WorldUp;
    float Yaw, Pitch, Sensitivity;
    Camera() : Position(50.0f, 20.0f, 80.0f), WorldUp(0.0f, 1.0f, 0.0f), Yaw(-90.0f), Pitch(0.0f), Sensitivity(0.1f) { updateCameraVectors(); }
    glm::mat4 GetViewMatrix() { return glm::lookAt(Position, Position + Front, Up); }
    void ProcessMouseMovement(float xoffset, float yoffset)
    {
        xoffset *= Sensitivity;
        yoffset *= Sensitivity;
        Yaw += xoffset;
        Pitch += yoffset;
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;
        updateCameraVectors();
    }
    void updateCameraVectors()
    {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }
};

Camera camera;
float lastX = SCREEN_W * 0.5f, lastY = SCREEN_H * 0.5f;
bool firstMouse = true;
float deltaTime = 0.0f, lastFrame = 0.0f;
bool leftPressed = false, rightPressed = false;
bool leftHeld = false;

// ★ 滚轮回调
double scrollY = 0.0;
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    scrollY += yoffset;
}

void mouse_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    float xoffset = xpos - lastX, yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    camera.ProcessMouseMovement(xoffset, yoffset);
}
void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        leftPressed = true;
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        rightPressed = true;
}

struct Frustum
{
    glm::vec4 planes[6];
};
Frustum extractFrustum(const glm::mat4 &m)
{
    Frustum f;
    f.planes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);
    f.planes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);
    f.planes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);
    f.planes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);
    f.planes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]);
    f.planes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);
    for (int i = 0; i < 6; i++)
        f.planes[i] /= glm::length(glm::vec3(f.planes[i]));
    return f;
}
bool isBoxInFrustum(const Frustum &f, const glm::vec3 &min, const glm::vec3 &max)
{
    for (int i = 0; i < 6; i++)
    {
        glm::vec3 p = min;
        if (f.planes[i].x >= 0)
            p.x = max.x;
        if (f.planes[i].y >= 0)
            p.y = max.y;
        if (f.planes[i].z >= 0)
            p.z = max.z;
        if (glm::dot(glm::vec3(f.planes[i]), p) + f.planes[i].w < 0)
            return false;
    }
    return true;
}

struct BlockType
{
    std::string top, bottom, north, south, west, east;
    glm::vec3 tT, tB, tN, tS, tW, tE;
    float hardness;
};

const int WORLD_SIZE = 100, WORLD_HEIGHT = 21, WORLD_Y_MIN = -10;
const int CHUNK_SIZE = 10;
const int CHUNKS_X = WORLD_SIZE / CHUNK_SIZE;
const int CHUNKS_Z = WORLD_SIZE / CHUNK_SIZE;
static std::string world[WORLD_SIZE][WORLD_HEIGHT][WORLD_SIZE];

struct Chunk
{
    unsigned int VAO, VBO;
    int vertexCount;
    glm::vec3 center;
    int cx, cz;
};
std::vector<Chunk> chunks;
static bool chunkDirty[CHUNKS_X][CHUNKS_Z];

void buildChunkMesh(int cx, int cz,
                    const std::map<std::string, BlockType> &types,
                    const std::map<std::string, int> &layerMap);

bool isWorldBlockEmpty(int x, int y, int z)
{
    if (x < 0 || x >= WORLD_SIZE || y < 0 || y >= WORLD_HEIGHT || z < 0 || z >= WORLD_SIZE)
        return true;
    return world[x][y][z].empty();
}

void markChunkDirty(int cx, int cz)
{
    if (cx < 0 || cx >= CHUNKS_X || cz < 0 || cz >= CHUNKS_Z)
        return;
    chunkDirty[cx][cz] = true;
}

void markChunkDirtyAround(int x, int z)
{
    int cx = x / CHUNK_SIZE;
    int cz = z / CHUNK_SIZE;
    markChunkDirty(cx, cz);
    markChunkDirty(cx + 1, cz);
    markChunkDirty(cx - 1, cz);
    markChunkDirty(cx, cz + 1);
    markChunkDirty(cx, cz - 1);
}

void rebuildDirtyChunks(const std::map<std::string, BlockType> &blockTypes,
                        const std::map<std::string, int> &layerMap)
{
    for (int cx = 0; cx < CHUNKS_X; cx++)
    {
        for (int cz = 0; cz < CHUNKS_Z; cz++)
        {
            if (!chunkDirty[cx][cz])
                continue;
            int index = -1;
            for (size_t i = 0; i < chunks.size(); i++)
            {
                if (chunks[i].cx == cx && chunks[i].cz == cz)
                {
                    index = (int)i;
                    break;
                }
            }
            if (index >= 0)
            {
                glDeleteVertexArrays(1, &chunks[index].VAO);
                glDeleteBuffers(1, &chunks[index].VBO);
                chunks.erase(chunks.begin() + index);
            }
            buildChunkMesh(cx, cz, blockTypes, layerMap);
            chunkDirty[cx][cz] = false;
        }
    }
}

static const float FACE_VERTS[6][6][3] = {
    {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}},
    {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}},
    {{-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}},
    {{0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}},
    {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}},
    {{-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}};

static const float FACE_UV[6][6][2] = {
    {{0, 1}, {1, 1}, {1, 0}, {1, 0}, {0, 0}, {0, 1}},
    {{0, 1}, {1, 1}, {1, 0}, {1, 0}, {0, 0}, {0, 1}},
    {{1, 1}, {1, 0}, {0, 0}, {0, 0}, {0, 1}, {1, 1}},
    {{0, 1}, {0, 0}, {1, 0}, {1, 0}, {1, 1}, {0, 1}},
    {{0, 0}, {0, 1}, {1, 1}, {1, 1}, {1, 0}, {0, 0}},
    {{0, 0}, {0, 1}, {1, 1}, {1, 1}, {1, 0}, {0, 0}}};

static const float FACE_NORMALS[6][3] = {
    {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};

void buildChunkMesh(int cx, int cz,
                    const std::map<std::string, BlockType> &types,
                    const std::map<std::string, int> &layerMap)
{
    std::vector<float> data;
    for (int x = cx * CHUNK_SIZE; x < (cx + 1) * CHUNK_SIZE; x++)
    {
        for (int z = cz * CHUNK_SIZE; z < (cz + 1) * CHUNK_SIZE; z++)
        {
            for (int yi = 0; yi < WORLD_HEIGHT; yi++)
            {
                if (world[x][yi][z].empty())
                    continue;
                float worldY = (float)(yi + WORLD_Y_MIN);
                const BlockType &bt = types.at(world[x][yi][z]);
                for (int i = 0; i < 6; i++)
                {
                    int nx = x, nyi = yi, nz = z;
                    switch (i)
                    {
                    case 0:
                        nz -= 1;
                        break;
                    case 1:
                        nz += 1;
                        break;
                    case 2:
                        nx -= 1;
                        break;
                    case 3:
                        nx += 1;
                        break;
                    case 4:
                        nyi -= 1;
                        break;
                    case 5:
                        nyi += 1;
                        break;
                    }
                    if (nx >= 0 && nx < WORLD_SIZE && nyi >= 0 && nyi < WORLD_HEIGHT && nz >= 0 && nz < WORLD_SIZE)
                        if (!world[nx][nyi][nz].empty())
                            continue;
                    std::string texName;
                    glm::vec3 tint(1.0f);
                    switch (i)
                    {
                    case 0:
                        texName = bt.north;
                        tint = bt.tN;
                        break;
                    case 1:
                        texName = bt.south;
                        tint = bt.tS;
                        break;
                    case 2:
                        texName = bt.west;
                        tint = bt.tW;
                        break;
                    case 3:
                        texName = bt.east;
                        tint = bt.tE;
                        break;
                    case 4:
                        texName = bt.bottom;
                        tint = bt.tB;
                        break;
                    case 5:
                        texName = bt.top;
                        tint = bt.tT;
                        break;
                    }
                    int layer = layerMap.at(texName);
                    for (int v = 0; v < 6; v++)
                    {
                        data.push_back(FACE_VERTS[i][v][0] + x);
                        data.push_back(FACE_VERTS[i][v][1] + worldY);
                        data.push_back(FACE_VERTS[i][v][2] + z);
                        data.push_back(FACE_UV[i][v][0]);
                        data.push_back(FACE_UV[i][v][1]);
                        data.push_back((float)layer);
                        data.push_back(FACE_NORMALS[i][0]);
                        data.push_back(FACE_NORMALS[i][1]);
                        data.push_back(FACE_NORMALS[i][2]);
                        data.push_back(tint.x);
                        data.push_back(tint.y);
                        data.push_back(tint.z);
                    }
                }
            }
        }
    }
    Chunk c;
    c.vertexCount = (int)(data.size() / 12);
    c.center = glm::vec3((cx + 0.5f) * CHUNK_SIZE, 0.0f, (cz + 0.5f) * CHUNK_SIZE);
    c.cx = cx;
    c.cz = cz;
    glGenVertexArrays(1, &c.VAO);
    glGenBuffers(1, &c.VBO);
    glBindVertexArray(c.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, c.VBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    int stride = 12 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void *)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void *)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);
    chunks.push_back(c);
}

bool checkCollision(glm::vec3 pos)
{
    const float R = 0.3f, H = 1.8f;
    if (pos.y < WORLD_Y_MIN - 0.5f)
        return true;
    glm::vec3 pmin(pos.x - R, pos.y, pos.z - R), pmax(pos.x + R, pos.y + H, pos.z + R);
    for (int x = (int)floor(pmin.x - 0.5f); x <= (int)floor(pmax.x + 0.5f); x++)
        for (int y = (int)floor(pmin.y - 0.5f); y <= (int)floor(pmax.y + 0.5f); y++)
            for (int z = (int)floor(pmin.z - 0.5f); z <= (int)floor(pmax.z + 0.5f); z++)
            {
                int yi = y - WORLD_Y_MIN;
                if (x < 0 || x >= WORLD_SIZE || yi < 0 || yi >= WORLD_HEIGHT || z < 0 || z >= WORLD_SIZE)
                    continue;
                if (world[x][yi][z].empty())
                    continue;
                glm::vec3 bmin(x - 0.5f, (float)y - 0.5f, z - 0.5f);
                glm::vec3 bmax(x + 0.5f, (float)y + 0.5f, z + 0.5f);
                if (pmin.x < bmax.x && pmax.x > bmin.x &&
                    pmin.y < bmax.y && pmax.y > bmin.y &&
                    pmin.z < bmax.z && pmax.z > bmin.z)
                    return true;
            }
    return false;
}

// ===== 文字系统 =====
unsigned int textVAO, textVBO, textProg, textProjLoc;
void initText()
{
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &textVertexShader, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &textFragmentShader, NULL);
    glCompileShader(fs);
    textProg = glCreateProgram();
    glAttachShader(textProg, vs);
    glAttachShader(textProg, fs);
    glLinkProgram(textProg);
    glDeleteShader(vs);
    glDeleteShader(fs);
    textProjLoc = glGetUniformLocation(textProg, "projection");
    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void drawText(const std::string &text, float x, float y, float scale, glm::vec3 color)
{
    static char buffer[99999];
    int numQuads = stb_easy_font_print(x, y, const_cast<char *>(text.c_str()), NULL, buffer, sizeof(buffer));
    glm::mat4 proj = glm::ortho(0.0f, (float)SCREEN_W, (float)SCREEN_H, 0.0f, -1.0f, 1.0f);
    glUseProgram(textProg);
    glUniformMatrix4fv(textProjLoc, 1, GL_FALSE, glm::value_ptr(proj));
    std::vector<float> triVerts;
    float *ptr = (float *)buffer;
    for (int q = 0; q < numQuads; q++)
    {
        int base = q * 4;
        int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++)
        {
            int i = base + idx[k];
            triVerts.push_back(ptr[i * 4 + 0] * scale);
            triVerts.push_back(ptr[i * 4 + 1] * scale);
            triVerts.push_back(color.r);
            triVerts.push_back(color.g);
            triVerts.push_back(color.b);
            triVerts.push_back(1.0f);
        }
    }
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, triVerts.size() * sizeof(float), triVerts.data(), GL_DYNAMIC_DRAW);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, triVerts.size() / 6);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void drawRect(float x, float y, float w, float h, glm::vec3 color)
{
    std::vector<float> verts = {
        x, y + h, color.r, color.g, color.b, 1.0f,
        x + w, y + h, color.r, color.g, color.b, 1.0f,
        x + w, y, color.r, color.g, color.b, 1.0f,
        x, y + h, color.r, color.g, color.b, 1.0f,
        x + w, y, color.r, color.g, color.b, 1.0f,
        x, y, color.r, color.g, color.b, 1.0f};
    glm::mat4 proj = glm::ortho(0.0f, (float)SCREEN_W, (float)SCREEN_H, 0.0f, -1.0f, 1.0f);
    glUseProgram(textProg);
    glUniformMatrix4fv(textProjLoc, 1, GL_FALSE, glm::value_ptr(proj));
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void drawCrosshair(float cx, float cy, float size, float thickness, glm::vec3 color)
{
    drawRect(cx - size, cy - thickness * 0.5f, size * 2.0f, thickness, color);
    drawRect(cx - thickness * 0.5f, cy - size, thickness, size * 2.0f, color);
}

// ===== 破坏层 =====
unsigned int breakVAO, breakVBO, breakProg;
unsigned int breakModelLoc, breakViewLoc, breakProjLoc, breakLayerLoc, breakTexLoc;

void initBreakCube()
{
    std::vector<float> verts;
    for (int i = 0; i < 6; i++)
        for (int v = 0; v < 6; v++)
        {
            verts.push_back(FACE_VERTS[i][v][0]);
            verts.push_back(FACE_VERTS[i][v][1]);
            verts.push_back(FACE_VERTS[i][v][2]);
            verts.push_back(FACE_UV[i][v][0]);
            verts.push_back(FACE_UV[i][v][1]);
        }
    glGenVertexArrays(1, &breakVAO);
    glGenBuffers(1, &breakVBO);
    glBindVertexArray(breakVAO);
    glBindBuffer(GL_ARRAY_BUFFER, breakVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &breakVertexShader, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &breakFragmentShader, NULL);
    glCompileShader(fs);
    breakProg = glCreateProgram();
    glAttachShader(breakProg, vs);
    glAttachShader(breakProg, fs);
    glLinkProgram(breakProg);
    glDeleteShader(vs);
    glDeleteShader(fs);
    breakModelLoc = glGetUniformLocation(breakProg, "model");
    breakViewLoc = glGetUniformLocation(breakProg, "view");
    breakProjLoc = glGetUniformLocation(breakProg, "projection");
    breakLayerLoc = glGetUniformLocation(breakProg, "layer");
    breakTexLoc = glGetUniformLocation(breakProg, "texArray");
}

// ===== 等轴测图标 =====
unsigned int iconVAO, iconVBO, iconProg;
unsigned int iconProjLoc, iconOffsetLoc, iconScaleLoc, iconLayerLoc, iconTintLoc, iconTexLoc;

static const float ISO_TOP[6][4] = {
    {0.5f, 0.0f, 0.5f, 0.0f},
    {1.0f, 0.25f, 1.0f, 0.25f},
    {0.5f, 0.5f, 0.5f, 0.5f},
    {0.5f, 0.0f, 0.5f, 0.0f},
    {0.5f, 0.5f, 0.5f, 0.5f},
    {0.0f, 0.25f, 0.0f, 0.25f}};
static const float ISO_LEFT[6][4] = {
    {0.0f, 0.25f, 0.0f, 0.0f},
    {0.0f, 0.75f, 0.0f, 1.0f},
    {0.5f, 1.0f, 1.0f, 1.0f},
    {0.0f, 0.25f, 0.0f, 0.0f},
    {0.5f, 1.0f, 1.0f, 1.0f},
    {0.5f, 0.5f, 1.0f, 0.0f}};
static const float ISO_RIGHT[6][4] = {
    {0.5f, 0.5f, 0.0f, 0.0f},
    {0.5f, 1.0f, 0.0f, 1.0f},
    {1.0f, 0.75f, 1.0f, 1.0f},
    {0.5f, 0.5f, 0.0f, 0.0f},
    {1.0f, 0.75f, 1.0f, 1.0f},
    {1.0f, 0.25f, 1.0f, 0.0f}};

void initIcon()
{
    glGenVertexArrays(1, &iconVAO);
    glGenBuffers(1, &iconVBO);
    glBindVertexArray(iconVAO);
    glBindBuffer(GL_ARRAY_BUFFER, iconVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &iconVertexShader, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &iconFragmentShader, NULL);
    glCompileShader(fs);
    iconProg = glCreateProgram();
    glAttachShader(iconProg, vs);
    glAttachShader(iconProg, fs);
    glLinkProgram(iconProg);
    glDeleteShader(vs);
    glDeleteShader(fs);
    iconProjLoc = glGetUniformLocation(iconProg, "projection");
    iconOffsetLoc = glGetUniformLocation(iconProg, "offset");
    iconScaleLoc = glGetUniformLocation(iconProg, "scale");
    iconLayerLoc = glGetUniformLocation(iconProg, "layer");
    iconTintLoc = glGetUniformLocation(iconProg, "tint");
    iconTexLoc = glGetUniformLocation(iconProg, "texArray");
}

void drawIsoIcon(float x, float y, float size, const std::string &blockName,
                 const std::map<std::string, BlockType> &blockTypes,
                 const std::map<std::string, int> &layerMap,
                 unsigned int texArray)
{
    const BlockType &bt = blockTypes.at(blockName);
    glm::mat4 proj = glm::ortho(0.0f, (float)SCREEN_W, (float)SCREEN_H, 0.0f, -1.0f, 1.0f);
    glUseProgram(iconProg);
    glUniformMatrix4fv(iconProjLoc, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1f(iconScaleLoc, size);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
    glUniform1i(iconTexLoc, 0);

    auto drawFace = [&](const float face[6][4], const std::string &texName, glm::vec3 tint)
    {
        std::vector<float> data;
        for (int i = 0; i < 6; i++)
        {
            data.push_back(face[i][0]);
            data.push_back(face[i][1]);
            data.push_back(face[i][2]);
            data.push_back(face[i][3]);
        }
        glUniform2f(iconOffsetLoc, x, y);
        glUniform1f(iconLayerLoc, (float)layerMap.at(texName));
        glUniform3f(iconTintLoc, tint.x, tint.y, tint.z);
        glBindVertexArray(iconVAO);
        glBindBuffer(GL_ARRAY_BUFFER, iconVBO);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
    };

    drawFace(ISO_TOP, bt.top, bt.tT);
    drawFace(ISO_LEFT, bt.west, bt.tW);
    drawFace(ISO_RIGHT, bt.east, bt.tE);
    glBindVertexArray(0);
}

// ===== 重建区块 =====
void rebuildAllChunks(const std::map<std::string, BlockType> &blockTypes,
                      const std::map<std::string, int> &layerMap)
{
    for (int cx = 0; cx < CHUNKS_X; cx++)
        for (int cz = 0; cz < CHUNKS_Z; cz++)
            chunkDirty[cx][cz] = true;
    rebuildDirtyChunks(blockTypes, layerMap);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(SCREEN_W, SCREEN_H, "Minecraft", NULL, NULL);
    if (!window)
        return -1;
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback); // ★ 滚轮
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        return -1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    std::vector<std::string> texNames = {
        "grass_block_top", "grass_block_side", "dirt",
        "stone", "ore_coal", "ore_iron",
        "destroy_stage_0", "destroy_stage_1", "destroy_stage_2", "destroy_stage_3", "destroy_stage_4",
        "destroy_stage_5", "destroy_stage_6", "destroy_stage_7", "destroy_stage_8", "destroy_stage_9"};
    std::map<std::string, int> layerMap;
    unsigned int texArray = loadTextureArray("./block", texNames, layerMap);

    std::map<std::string, BlockType> blockTypes = {
        {"grass", {"grass_block_top", "dirt", "grass_block_side", "grass_block_side", "grass_block_side", "grass_block_side", glm::vec3(0.36f, 0.61f, 0.24f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), 0.6f}},
        {"dirt", {"dirt", "dirt", "dirt", "dirt", "dirt", "dirt", glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), 0.5f}},
        {"stone", {"stone", "stone", "stone", "stone", "stone", "stone", glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), 1.5f}},
        {"coal_ore", {"ore_coal", "ore_coal", "ore_coal", "ore_coal", "ore_coal", "ore_coal", glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), 3.0f}},
        {"iron_ore", {"ore_iron", "ore_iron", "ore_iron", "ore_iron", "ore_iron", "ore_iron", glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), 3.0f}}};

    for (int x = 0; x < WORLD_SIZE; x++)
        for (int y = 0; y < WORLD_HEIGHT; y++)
            for (int z = 0; z < WORLD_SIZE; z++)
                world[x][y][z] = "";
    for (int cx = 0; cx < CHUNKS_X; cx++)
        for (int cz = 0; cz < CHUNKS_Z; cz++)
            chunkDirty[cx][cz] = true;

    const siv::PerlinNoise perlin{12345u};
    const siv::PerlinNoise orePerlin{67890u};

    for (int x = 0; x < WORLD_SIZE; x++)
    {
        for (int z = 0; z < WORLD_SIZE; z++)
        {
            double n = perlin.octave2D_01(x * 0.05, z * 0.05, 6);
            int height = (int)(n * 20.0) - 10;
            for (int y = WORLD_Y_MIN; y <= height; y++)
            {
                int yi = y - WORLD_Y_MIN;
                if (yi < 0 || yi >= WORLD_HEIGHT)
                    continue;
                if (y == height)
                    world[x][yi][z] = "grass";
                else if (y >= height - 1)
                    world[x][yi][z] = "dirt";
                else
                {
                    double coal = orePerlin.noise3D(x * 0.3, y * 0.3, z * 0.3);
                    double iron = orePerlin.noise3D(x * 0.5 + 100.0, y * 0.5, z * 0.5 + 100.0);
                    if (coal > 0.6 && y < height - 3)
                        world[x][yi][z] = "coal_ore";
                    else if (iron > 0.7 && y < height - 5 && y < 0)
                        world[x][yi][z] = "iron_ore";
                    else
                        world[x][yi][z] = "stone";
                }
            }
        }
    }

    for (int cx = 0; cx < CHUNKS_X; cx++)
        for (int cz = 0; cz < CHUNKS_Z; cz++)
            buildChunkMesh(cx, cz, blockTypes, layerMap);

    initText();
    initBreakCube();
    initIcon();

    unsigned int modelLoc = glGetUniformLocation(prog, "model");
    unsigned int viewLoc = glGetUniformLocation(prog, "view");
    unsigned int projLoc = glGetUniformLocation(prog, "projection");
    unsigned int lightPosLoc = glGetUniformLocation(prog, "lightPos");
    unsigned int viewPosLoc = glGetUniformLocation(prog, "viewPos");
    unsigned int lightColorLoc = glGetUniformLocation(prog, "lightColor");
    unsigned int texArrayLoc = glGetUniformLocation(prog, "texArray");
    unsigned int ambientLoc = glGetUniformLocation(prog, "ambientStrength");

    glm::vec3 playerPos(50.0f, 15.0f, 50.0f);
    float playerVelY = 0.0f;
    bool onGround = false;
    float timeOfDay = 0.25f;
    int frameCount = 0;
    float fpsTimer = 0.0f;

    int targetX = -1, targetY = -1, targetZ = -1;
    float breakProgress = 0.0f;

    int health = 20;
    float fallStartY = playerPos.y;
    float fallStartVelY = 0.0f;
    bool wasOnGround = true;

    // ★ 重力（可调）
    float gravity = -25.0f;

    std::vector<std::string> hotbar = {"grass", "dirt", "stone", "coal_ore", "iron_ore", "stone", "stone", "stone", "stone"};
    int selectedSlot = 2;

    while (!glfwWindowShouldClose(window))
    {
        float cf = glfwGetTime();
        deltaTime = cf - lastFrame;
        lastFrame = cf;
        timeOfDay += deltaTime * 0.02f;
        if (timeOfDay > 1.0f)
            timeOfDay -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        leftHeld = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (!leftHeld)
            leftPressed = false;

        // ★ 滚轮切换物品栏
        if (scrollY != 0.0)
        {
            if (scrollY > 0)
                selectedSlot = (selectedSlot + 1) % 9;
            else
                selectedSlot = (selectedSlot + 8) % 9;
            scrollY = 0.0;
        }

        // 数字键切换
        for (int i = 0; i < 9; i++)
        {
            if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS)
                selectedSlot = i;
        }

        // ★ 重力调节：[ 减小，] 增大，\ 重置
        if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
        {
            gravity -= 20.0f * deltaTime;
            if (gravity < -100.0f)
                gravity = -100.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
        {
            gravity += 20.0f * deltaTime;
            if (gravity > 20.0f)
                gravity = 20.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_BACKSLASH) == GLFW_PRESS)
        {
            gravity = -20.0f;
        }

        glm::vec3 moveDir(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            moveDir += glm::vec3(camera.Front.x, 0.0f, camera.Front.z);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            moveDir -= glm::vec3(camera.Front.x, 0.0f, camera.Front.z);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            moveDir -= camera.Right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            moveDir += camera.Right;
        if (glm::length(moveDir) > 0.0f)
            moveDir = glm::normalize(moveDir);
        glm::vec3 newPos = playerPos + moveDir * 5.0f * deltaTime;
        if (!checkCollision(glm::vec3(newPos.x, playerPos.y, playerPos.z)))
            playerPos.x = newPos.x;
        if (!checkCollision(glm::vec3(playerPos.x, playerPos.y, newPos.z)))
            playerPos.z = newPos.z;

        playerVelY += gravity * deltaTime; // ★ 用可调重力
        glm::vec3 testY = playerPos;
        testY.y += playerVelY * deltaTime;
        bool newOnGround = false;
        if (!checkCollision(testY))
            playerPos.y = testY.y;
        else
        {
            playerVelY = 0.0f;
            newOnGround = true;
        }

        if (!newOnGround)
        {
            if (wasOnGround)
                fallStartY = playerPos.y;
            fallStartVelY = playerVelY;
        }
        else
        {
            if (wasOnGround == false && fallStartVelY < -8.0f)
            {
                float fallDistance = fallStartY - playerPos.y;
                if (fallDistance > 3.0f)
                {
                    int damage = (int)(fallDistance - 3.0f);
                    health -= damage;
                    if (health < 0)
                        health = 0;
                }
            }
        }
        wasOnGround = newOnGround;
        onGround = newOnGround;

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && onGround)
        {
            playerVelY = 8.0f;
            onGround = false;
        }

        if (playerPos.y < WORLD_Y_MIN - 10.0f)
            health = 0;

        {
            glm::vec3 headPos = playerPos + glm::vec3(0.0f, 1.62f, 0.0f);
            int hx2 = (int)floor(headPos.x + 0.5f);
            int hy2 = (int)floor(headPos.y + 0.5f);
            int hz2 = (int)floor(headPos.z + 0.5f);
            int hyi2 = hy2 - WORLD_Y_MIN;
            if (hx2 >= 0 && hx2 < WORLD_SIZE && hyi2 >= 0 && hyi2 < WORLD_HEIGHT && hz2 >= 0 && hz2 < WORLD_SIZE)
            {
                if (!world[hx2][hyi2][hz2].empty())
                {
                    static float suffocateTimer = 0.0f;
                    suffocateTimer += deltaTime;
                    if (suffocateTimer >= 1.0f)
                    {
                        health -= 1;
                        if (health < 0)
                            health = 0;
                        suffocateTimer = 0.0f;
                    }
                }
            }
        }

        if (health <= 0)
        {
            playerPos = glm::vec3(50.0f, 15.0f, 50.0f);
            playerVelY = 0.0f;
            health = 20;
        }

        camera.Position = playerPos + glm::vec3(0.0f, 1.62f, 0.0f);

        // 射线检测
        int hx = -1, hy = -1, hz = -1;
        bool hit = false;
        for (float t = 0.0f; t < 8.0f; t += 0.05f)
        {
            glm::vec3 p = camera.Position + camera.Front * t;
            int x = (int)floor(p.x + 0.5f);
            int y = (int)floor(p.y + 0.5f);
            int z = (int)floor(p.z + 0.5f);
            int yi = y - WORLD_Y_MIN;
            if (x < 0 || x >= WORLD_SIZE || yi < 0 || yi >= WORLD_HEIGHT || z < 0 || z >= WORLD_SIZE)
                continue;
            if (!world[x][yi][z].empty())
            {
                hx = x;
                hy = yi;
                hz = z;
                hit = true;
                break;
            }
        }

        if (hx != targetX || hy != targetY || hz != targetZ)
        {
            targetX = hx;
            targetY = hy;
            targetZ = hz;
            breakProgress = 0.0f;
        }

        if (leftHeld && hit)
        {
            std::string blockName = world[hx][hy][hz];
            float hardness = blockTypes.at(blockName).hardness;
            breakProgress += deltaTime / hardness;
            if (breakProgress >= 1.0f)
            {
                world[hx][hy][hz] = "";
                markChunkDirtyAround(hx, hz);
                breakProgress = 0.0f;
                targetX = targetY = targetZ = -1;
                rebuildDirtyChunks(blockTypes, layerMap);
            }
        }
        else
        {
            breakProgress = 0.0f;
        }

        if (rightPressed && hit)
        {
            int nx = hx, nyi = hy, nz = hz;
            glm::vec3 diff = camera.Position - glm::vec3(hx, hy + WORLD_Y_MIN, hz);
            glm::vec3 absDiff = glm::abs(diff);
            if (absDiff.x > absDiff.y && absDiff.x > absDiff.z)
                nx += (diff.x > 0 ? 1 : -1);
            else if (absDiff.y > absDiff.z)
                nyi += (diff.y > 0 ? 1 : -1);
            else
                nz += (diff.z > 0 ? 1 : -1);

            if (nx >= 0 && nx < WORLD_SIZE && nyi >= 0 && nyi < WORLD_HEIGHT && nz >= 0 && nz < WORLD_SIZE)
            {
                glm::vec3 placePos(nx, nyi + WORLD_Y_MIN, nz);
                glm::vec3 pmin = playerPos + glm::vec3(-0.3f, 0.0f, -0.3f);
                glm::vec3 pmax = playerPos + glm::vec3(0.3f, 1.8f, 0.3f);
                bool intersectsPlayer = (placePos.x + 0.5f > pmin.x && placePos.x - 0.5f < pmax.x &&
                                         placePos.y + 0.5f > pmin.y && placePos.y - 0.5f < pmax.y &&
                                         placePos.z + 0.5f > pmin.z && placePos.z - 0.5f < pmax.z);
                if (!intersectsPlayer && world[nx][nyi][nz].empty())
                {
                    world[nx][nyi][nz] = hotbar[selectedSlot];
                    markChunkDirtyAround(nx, nz);
                    rebuildDirtyChunks(blockTypes, layerMap);
                }
            }
            rightPressed = false;
        }

        float sunHeight = sin(timeOfDay * 2.0f * 3.14159f);
        glm::vec3 skyColor;
        if (sunHeight > 0.3f)
            skyColor = glm::vec3(0.5f, 0.7f, 1.0f);
        else if (sunHeight > 0.0f)
            skyColor = glm::mix(glm::vec3(0.9f, 0.4f, 0.2f), glm::vec3(0.5f, 0.7f, 1.0f), sunHeight / 0.3f);
        else if (sunHeight > -0.3f)
            skyColor = glm::mix(glm::vec3(0.9f, 0.4f, 0.2f), glm::vec3(0.05f, 0.05f, 0.15f), -sunHeight / 0.3f);
        else
            skyColor = glm::vec3(0.05f, 0.05f, 0.15f);
        glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 sunPos(cos(timeOfDay * 2.0f * 3.14159f) * 200.0f, sunHeight * 200.0f, 0.0f);
        glm::vec3 sunColor;
        if (sunHeight > 0.3f)
            sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
        else if (sunHeight > 0.0f)
            sunColor = glm::mix(glm::vec3(1.0f, 0.5f, 0.2f), glm::vec3(1.0f, 1.0f, 1.0f), sunHeight / 0.3f);
        else if (sunHeight > -0.3f)
            sunColor = glm::mix(glm::vec3(1.0f, 0.5f, 0.2f), glm::vec3(0.3f, 0.3f, 0.5f), -sunHeight / 0.3f);
        else
            sunColor = glm::vec3(0.3f, 0.3f, 0.5f);
        float ambientStrength = glm::clamp(sunHeight * 0.15f + 0.25f, 0.25f, 0.4f);

        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(70.0f), (float)SCREEN_W / SCREEN_H, 0.1f, 500.0f);
        Frustum frustum = extractFrustum(projection * view);

        glUseProgram(prog);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(lightPosLoc, sunPos.x, sunPos.y, sunPos.z);
        glUniform3f(viewPosLoc, camera.Position.x, camera.Position.y, camera.Position.z);
        glUniform3f(lightColorLoc, sunColor.x, sunColor.y, sunColor.z);
        glUniform1f(ambientLoc, ambientStrength);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
        glUniform1i(texArrayLoc, 0);

        glm::mat4 identity(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(identity));

        for (const auto &c : chunks)
        {
            glm::vec3 cmin = c.center - glm::vec3(CHUNK_SIZE, WORLD_HEIGHT, CHUNK_SIZE);
            glm::vec3 cmax = c.center + glm::vec3(CHUNK_SIZE, WORLD_HEIGHT, CHUNK_SIZE);
            if (!isBoxInFrustum(frustum, cmin, cmax))
                continue;
            glBindVertexArray(c.VAO);
            glDrawArrays(GL_TRIANGLES, 0, c.vertexCount);
        }

        if (breakProgress > 0.0f && targetX >= 0)
        {
            int stage = (int)(breakProgress * 10.0f);
            if (stage > 9)
                stage = 9;
            int layer = layerMap.at("destroy_stage_" + std::to_string(stage));
            glm::vec3 blockPos(targetX, targetY + WORLD_Y_MIN, targetZ);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), blockPos) * glm::scale(glm::mat4(1.0f), glm::vec3(1.002f));
            glUseProgram(breakProg);
            glUniformMatrix4fv(breakModelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(breakViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(breakProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1f(breakLayerLoc, (float)layer);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
            glUniform1i(breakTexLoc, 0);
            glBindVertexArray(breakVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        frameCount++;
        fpsTimer += deltaTime;
        static float lastFPS = 0.0f;
        if (fpsTimer >= 0.5f)
        {
            lastFPS = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        drawCrosshair(SCREEN_W * 0.5f, SCREEN_H * 0.5f, 10.0f, 2.0f, glm::vec3(1, 1, 1));

        char buf[128];
        snprintf(buf, sizeof(buf), "FPS: %d", (int)lastFPS);
        drawText(buf, 10.0f, 10.0f, 2.0f, glm::vec3(1, 1, 1));
        snprintf(buf, sizeof(buf), "Pos: %.0f, %.0f, %.0f", playerPos.x, playerPos.y, playerPos.z);
        drawText(buf, 10.0f, 30.0f, 2.0f, glm::vec3(1, 1, 1));
        snprintf(buf, sizeof(buf), "Gravity: %.1f", gravity);
        drawText(buf, 10.0f, 50.0f, 2.0f, glm::vec3(0.8f, 1.0f, 0.8f));
        snprintf(buf, sizeof(buf), "Slot: %d (%s)", selectedSlot + 1, hotbar[selectedSlot].c_str());
        drawText(buf, 10.0f, 70.0f, 2.0f, glm::vec3(1, 1, 0.5f));

        {
            float heartSize = 20.0f;
            float heartSpacing = 22.0f;
            float heartStartX = SCREEN_W * 0.5f - 5 * heartSpacing + heartSpacing * 0.5f;
            float heartY = SCREEN_H - 40.0f;
            for (int i = 0; i < 10; i++)
            {
                float hx = heartStartX + i * heartSpacing;
                int hp = health - i * 2;
                if (hp >= 2)
                    drawRect(hx, heartY, heartSize, heartSize, glm::vec3(1.0f, 0.0f, 0.0f));
                else if (hp == 1)
                {
                    drawRect(hx, heartY, heartSize * 0.5f, heartSize, glm::vec3(1.0f, 0.0f, 0.0f));
                    drawRect(hx + heartSize * 0.5f, heartY, heartSize * 0.5f, heartSize, glm::vec3(0.1f, 0.1f, 0.1f));
                }
                else
                    drawRect(hx, heartY, heartSize, heartSize, glm::vec3(0.1f, 0.1f, 0.1f));
            }
        }

        {
            float slotSize = 50.0f;
            float padding = 4.0f;
            float totalWidth = 9 * slotSize + 8 * padding;
            float startX = (SCREEN_W - totalWidth) * 0.5f;
            float slotY = SCREEN_H - 60.0f;

            drawRect(startX - 6, slotY - 6, totalWidth + 12, slotSize + 12, glm::vec3(0.2f, 0.2f, 0.2f));

            for (int i = 0; i < 9; i++)
            {
                float sx = startX + i * (slotSize + padding);
                glm::vec3 slotColor = (i == selectedSlot) ? glm::vec3(0.5f, 0.5f, 0.5f) : glm::vec3(0.3f, 0.3f, 0.3f);
                drawRect(sx, slotY, slotSize, slotSize, slotColor);
                drawIsoIcon(sx + 5.0f, slotY + 5.0f, slotSize - 10.0f, hotbar[i], blockTypes, layerMap, texArray);
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    return 0;
}