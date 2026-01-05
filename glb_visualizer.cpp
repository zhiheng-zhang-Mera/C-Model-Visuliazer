// glb_visualizer.cpp
// ---------------------------------------------------------
// 这是一个基于传统管线 (Legacy OpenGL) 的 GLB 查看器，简单直接，缺点是性能较差（使用立即模式渲染）。
// It's a Legacy OpenGL based GLB viewer. Simple but efficient.
// ---------------------------------------------------------

// 定义这些宏以在 tiny_gltf.h 中启用实现代码
// Define these macros to enable implementation in tiny_gltf.h
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// 在 Windows 上需要确保依赖库正确链接，避免外部图像库冲突
// On Windows, ensure dependencies link correctly and avoid external image lib conflicts
#define TINYGLTF_NO_EXTERNAL_IMAGE

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

// [外部依赖] 必须确保此文件存在于项目目录中
// [External Dependency] Ensure this file exists in the project directory
#include "tiny_gltf.h" 

// 处理跨平台 OpenGL 头文件包含
// Handle cross-platform OpenGL header inclusion
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

// --- 全局变量 (Global Variables) ---

// 存储扁平化的顶点位置数据 (x, y, z, x, y, z...)
// Stores flattened vertex position data
std::vector<float> g_positions;

// 存储扁平化的法线数据 (nx, ny, nz...)
// Stores flattened normal data
std::vector<float> g_normals;

// 存储顶点索引数据，用于定义三角形连接顺序
// Stores vertex indices to define triangle connectivity
std::vector<unsigned int> g_indices;

// 摄像机/模型旋转角度
// Camera/Model rotation angles
float angleX = 0.0f;
float angleY = 0.0f;

// 鼠标交互变量
// Mouse interaction variables
int lastMouseX, lastMouseY;
bool isDragging = false;


// --- 核心：GLB/glTF 加载器 (Core: GLB/glTF Loader) ---

// 辅助函数：从 glTF buffer accessor 中读取数据并拷贝到 std::vector
// Helper function: Read data from glTF buffer accessor and copy to std::vector
// 这是一个简化版本，只处理最常见的 FLOAT (GL_FLOAT) 格式
// Simplified version, only handles common FLOAT (GL_FLOAT) format
void get_buffer_data(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<float>& out_data) {
    // 获取 BufferView 和 Buffer
    // Get BufferView and Buffer
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    // 确保是 FLOAT 类型且为 VEC3 (3分量)
    // Ensure it is FLOAT type and VEC3 (3 components)
    if (accessor.componentType != GL_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3) {
        std::cerr << "错误: 仅支持 VEC3 (FLOAT) 类型的顶点和法线数据! (Error: Only VEC3 FLOAT supported)" << std::endl;
        return;
    }

    size_t count = accessor.count;
    size_t component_size = 3; // VEC3 (x, y, z)

    // 计算数据起始地址
    // Calculate data starting address
    const unsigned char* base_ptr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    // 获取步长 (Stride)，用于跳过数据
    // Get ByteStride to skip data
    size_t byte_stride = accessor.ByteStride(bufferView);

    out_data.resize(count * component_size);

    // 遍历并拷贝数据
    // Iterate and copy data
    for (size_t i = 0; i < count; ++i) {
        const float* current_data = reinterpret_cast<const float*>(base_ptr + i * byte_stride);
        out_data[i * component_size + 0] = current_data[0];
        out_data[i * component_size + 1] = current_data[1];
        out_data[i * component_size + 2] = current_data[2];
    }
}

// 辅助函数：从 glTF buffer accessor 中读取索引数据
// Helper function: Read index data from glTF buffer accessor
void get_index_data(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<unsigned int>& out_indices) {
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    size_t count = accessor.count;
    const unsigned char* base_ptr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

    out_indices.resize(count);

    // 根据索引数据类型（unsigned short 或 unsigned int）进行读取并统一转换为 int
    // Read based on index data type (short or int) and convert to int
    for (size_t i = 0; i < count; ++i) {
        if (accessor.componentType == GL_UNSIGNED_SHORT) {
            out_indices[i] = *reinterpret_cast<const unsigned short*>(base_ptr + i * 2);
        }
        else if (accessor.componentType == GL_UNSIGNED_INT) {
            out_indices[i] = *reinterpret_cast<const unsigned int*>(base_ptr + i * 4);
        }
        else {
            std::cerr << "错误: 索引类型不支持! (Error: Unsupported index type)" << std::endl;
            return;
        }
    }
}

// 主加载函数：解析 GLB 文件
// Main loading function: Parse GLB file
void loadGLB(const std::string& filename) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // 加载二进制 glTF 文件 (.glb)
    // Load binary glTF file
    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);

    if (!warn.empty()) {
        std::cout << "Warning: " << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << "Error: " << err << std::endl;
    }

    if (!ret) {
        std::cerr << "Failed to load GLB file: " << filename << std::endl;
        exit(1);
    }

    // 检查模型中是否有网格
    // Check if model has meshes
    if (model.meshes.empty() || model.meshes[0].primitives.empty()) {
        std::cerr << "Model contains no renderable mesh primitives." << std::endl;
        exit(1);
    }

    // --- 提取第一个 Mesh 的第一个 Primitive (Extract 1st Primitive of 1st Mesh) ---
    // 注意：真实应用应遍历所有 Mesh 和 Primitive
    // Note: Real apps should iterate all meshes/primitives
    const tinygltf::Primitive& primitive = model.meshes[0].primitives[0];

    // 1. 提取顶点位置 (POSITION)
    if (primitive.attributes.count("POSITION")) {
        int accessor_index = primitive.attributes.at("POSITION");
        get_buffer_data(model, model.accessors[accessor_index], g_positions);
        std::cout << "LoadedVertices: " << g_positions.size() / 3 << std::endl;
    }

    // 2. 提取法线 (NORMAL) - 用于光照计算
    if (primitive.attributes.count("NORMAL")) {
        int accessor_index = primitive.attributes.at("NORMAL");
        get_buffer_data(model, model.accessors[accessor_index], g_normals);
        std::cout << "Loaded Normals: " << g_normals.size() / 3 << std::endl;
    }

    // 3. 提取索引 (INDICES)
    if (primitive.indices > -1) {
        get_index_data(model, model.accessors[primitive.indices], g_indices);
        std::cout << "Loaded Indices: " << g_indices.size() << std::endl;
    }

    // 检查法线是否缺失
    if (g_normals.empty()) {
        std::cout << "法线数据缺失, 将禁用光照渲染。 (Normals missing, lighting disabled)" << std::endl;
    }
}

// --- 渲染相关 (Rendering) ---

// 初始化 OpenGL 设置
// Initialize OpenGL settings
void init() {
    glEnable(GL_DEPTH_TEST); // 开启深度测试，防止模型穿模 (Enable depth test)
    glEnable(GL_LIGHTING);   // 开启光照计算 (Enable lighting)
    glEnable(GL_LIGHT0);     // 开启 0 号光源 (Enable Light 0)

    // 设置材质颜色 (Diffuse)
    // Set material diffuse color
    GLfloat matDiff[] = { 0.8f, 0.8f, 0.8f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, matDiff);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // 填充模式 (Fill mode)
}

// 显示回调函数：每一帧的绘制逻辑
// Display callback: Drawing logic for each frame
void display() {
    // 清除颜色缓冲和深度缓冲
    // Clear color and depth buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity(); // 重置模型视图矩阵

    // 设置摄像机位置 (LookAt)
    // Set camera position
    gluLookAt(0.0, 0.0, 5.0,  // Eye position (眼睛位置)
        0.0, 0.0, 0.0,        // Center position (观察点)
        0.0, 1.0, 0.0);       // Up vector (头顶朝向)

    // 设置光源位置 (这里设置为头灯，跟随摄像机)
    // Set light position (Headlight, follows camera)
    if (!g_normals.empty()) {
        GLfloat lightPos[] = { 0.0f, 0.0f, 1.0f, 0.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    }
    else {
        glDisable(GL_LIGHTING); // 无发线则关闭光照，避免全黑
        glColor3f(0.7f, 0.7f, 0.7f);
    }

    // 应用鼠标控制的旋转
    // Apply mouse-controlled rotation
    glRotatef(angleX, 1.0f, 0.0f, 0.0f);
    glRotatef(angleY, 0.0f, 1.0f, 0.0f);

    // 绘制模型
    if (g_indices.empty()) {
        glutSwapBuffers();
        return;
    }

    // 开始绘制三角形 (Begin drawing triangles)
    // 爱丽丝备注：这是旧式 OpenGL 写法 (Immediate Mode)，效率较低但简单。
    glBegin(GL_TRIANGLES);

    for (size_t i = 0; i < g_indices.size(); ++i) {
        unsigned int index = g_indices[i];

        // 边界检查 (Boundary check)
        if (index * 3 + 2 < g_positions.size()) {

            // 应用法线（如果有）- 必须在顶点之前设置
            // Apply normal (if exists) - Must be set BEFORE vertex
            if (!g_normals.empty() && index * 3 + 2 < g_normals.size()) {
                glNormal3f(g_normals[index * 3 + 0],
                    g_normals[index * 3 + 1],
                    g_normals[index * 3 + 2]);
            }

            // 绘制顶点
            // Draw vertex
            glVertex3f(g_positions[index * 3 + 0],
                g_positions[index * 3 + 1],
                g_positions[index * 3 + 2]);
        }
    }
    glEnd(); // 结束绘制 (End drawing)

    glutSwapBuffers(); // 交换缓冲区 (双缓冲模式)
}

// 窗口大小改变回调
// Window reshape callback
void reshape(int w, int h) {
    if (h == 0) h = 1;
    float ratio = (float)w / h;
    glViewport(0, 0, w, h); // 设置视口
    glMatrixMode(GL_PROJECTION); // 切换到投影矩阵
    glLoadIdentity();
    gluPerspective(45.0f, ratio, 0.1f, 100.0f); // 设置透视投影
    glMatrixMode(GL_MODELVIEW); // 切换回模型视图矩阵
}

// --- 鼠标交互 (Mouse Interaction) ---
void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON) {
        if (state == GLUT_DOWN) {
            isDragging = true;
            lastMouseX = x;
            lastMouseY = y;
        }
        else {
            isDragging = false;
        }
    }
}

// 鼠标移动回调
void motion(int x, int y) {
    if (isDragging) {
        // 根据鼠标移动距离更新旋转角度
        // Update rotation angle based on mouse movement
        angleY += (x - lastMouseX) * 0.5f;
        angleX += (y - lastMouseY) * 0.5f;
        lastMouseX = x;
        lastMouseY = y;
        glutPostRedisplay(); // 请求重绘 (Request redraw)
    }
}

// --- 主函数 (Main Function) ---
int main(int argc, char** argv) {
    // 默认加载 model.glb，也可以通过命令行参数传入
    // Default load model.glb, or pass via command line args
    std::string filename = "model.glb";
    if (argc > 1) {
        filename = argv[1];
    }

    std::cout << "正在尝试加载 (Attempting to load): " << filename << std::endl;
    loadGLB(filename);

    // 初始化 GLUT
    glutInit(&argc, argv);
    // 设置显示模式：双缓冲、RGB颜色、深度缓冲
    // Display mode: Double buffer, RGB, Depth buffer
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(800, 600);
    glutCreateWindow("Simple GLB Viewer (Alice's Version)");

    init(); // 初始化渲染状态

    // 注册回调函数 (Register callbacks)
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);

    std::cout << "Starting main loop..." << std::endl;
    glutMainLoop(); // 进入主循环
    return 0;
}