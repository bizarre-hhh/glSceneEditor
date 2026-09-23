// 防止 Camera 类型在同一编译单元中被重复定义。
#ifndef CAMERA_H
// 定义头文件保护宏。
#define CAMERA_H

// 引入 GLboolean 等 OpenGL 基础类型。
#include <glad/glad.h>
// 引入 vec3 和 mat4 等 GLM 数学类型。
#include <glm/glm.hpp>
// 引入 lookAt、radians 等相机计算所需的矩阵变换函数。
#include <glm/gtc/matrix_transform.hpp>

// 用与具体窗口系统无关的枚举描述四种平面移动方向。
enum Camera_Movement
{
    // 沿相机 Front 方向前进。
    FORWARD,
    // 沿相机 Front 反方向后退。
    BACKWARD,
    // 沿相机 Right 反方向左移。
    LEFT,
    // 沿相机 Right 方向右移。
    RIGHT
};

// 默认偏航角为 -90°，使初始视线从 +X 转向 OpenGL 常用的 -Z。
const float YAW = -90.0f;
// 默认俯仰角为 0°，使初始视线保持水平。
const float PITCH = 0.0f;
// 默认键盘移动速度，单位约为“世界单位/秒”。
const float SPEED = 2.5f;
// 默认鼠标灵敏度，用来缩小原始像素偏移对角度的影响。
const float SENSITIVITY = 0.1f;
// 默认垂直视野角为 45°。
const float ZOOM = 45.0f;

// Camera 保存相机姿态，并把键鼠输入转换为观察矩阵。
class Camera
{
public:
    // 相机在世界空间中的位置。
    glm::vec3 Position;
    // 相机当前朝向的单位向量。
    glm::vec3 Front;
    // 相机局部坐标系中的上方向单位向量。
    glm::vec3 Up;
    // 相机局部坐标系中的右方向单位向量。
    glm::vec3 Right;
    // 世界坐标系固定的参考上方向，通常是 +Y。
    glm::vec3 WorldUp;

    // 绕世界 Y 轴旋转的偏航角，单位为度。
    float Yaw;
    // 上下观察的俯仰角，单位为度。
    float Pitch;

    // 当前键盘移动速度。
    float MovementSpeed;
    // 当前鼠标输入灵敏度。
    float MouseSensitivity;
    // 当前垂直视野角；数值越小，看起来放得越大。
    float Zoom;

    // 使用向量参数创建相机，并允许省略参数而采用默认姿态。
    Camera(
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
        float yaw = YAW,
        float pitch = PITCH
    )
        // 先给朝向、速度、灵敏度和视野角设置稳定的初值。
        : Front(glm::vec3(0.0f, 0.0f, -1.0f)),
          MovementSpeed(SPEED),
          MouseSensitivity(SENSITIVITY),
          Zoom(ZOOM)
    {
        // 保存调用方指定的世界空间位置。
        Position = position;
        // 保存世界上方向，后续用它构建相机正交基。
        WorldUp = up;
        // 保存初始偏航角。
        Yaw = yaw;
        // 保存初始俯仰角。
        Pitch = pitch;
        // 根据两个欧拉角计算真正的 Front、Right 和 Up。
        updateCameraVectors();
    }

    // 使用八个标量创建相机，方便没有 GLM 向量的调用方传参。
    Camera(
        float posX,
        float posY,
        float posZ,
        float upX,
        float upY,
        float upZ,
        float yaw,
        float pitch
    )
        // 仍然为可配置选项设置与向量构造函数相同的默认值。
        : Front(glm::vec3(0.0f, 0.0f, -1.0f)),
          MovementSpeed(SPEED),
          MouseSensitivity(SENSITIVITY),
          Zoom(ZOOM)
    {
        // 将三个位置标量组合为世界空间位置向量。
        Position = glm::vec3(posX, posY, posZ);
        // 将三个上方向标量组合为世界上方向向量。
        WorldUp = glm::vec3(upX, upY, upZ);
        // 保存调用方给出的偏航角。
        Yaw = yaw;
        // 保存调用方给出的俯仰角。
        Pitch = pitch;
        // 使用初始角度建立相机坐标轴。
        updateCameraVectors();
    }

    // 根据当前位置和方向返回世界空间到观察空间的变换矩阵。
    glm::mat4 GetViewMatrix() const
    {
        // 观察目标取 Position + Front，并使用相机自身的 Up 维持画面朝上。
        return glm::lookAt(Position, Position + Front, Up);
    }

    // 按某个方向移动相机，deltaTime 用于消除帧率差异。
    void ProcessKeyboard(Camera_Movement direction, float deltaTime)
    {
        // 本帧位移量等于“每秒速度 × 本帧秒数”。
        float velocity = MovementSpeed * deltaTime;
        // 前进时沿 Front 增加位置。
        if (direction == FORWARD)
            Position += Front * velocity;
        // 后退时沿 Front 减少位置。
        if (direction == BACKWARD)
            Position -= Front * velocity;
        // 左移时沿 Right 减少位置。
        if (direction == LEFT)
            Position -= Right * velocity;
        // 右移时沿 Right 增加位置。
        if (direction == RIGHT)
            Position += Right * velocity;
    }

    // 将鼠标的横纵偏移转换为偏航角和俯仰角变化。
    void ProcessMouseMovement(
        float xoffset,
        float yoffset,
        GLboolean constrainPitch = true
    )
    {
        // 用灵敏度缩放水平像素偏移，得到较平滑的角度变化。
        xoffset *= MouseSensitivity;
        // 用同样的灵敏度缩放竖直像素偏移。
        yoffset *= MouseSensitivity;

        // 水平偏移累加到偏航角。
        Yaw += xoffset;
        // 竖直偏移累加到俯仰角。
        Pitch += yoffset;

        // 默认限制俯仰角，避免越过正上方后画面翻转。
        if (constrainPitch)
        {
            // 超过向上 89° 时钳制在 89°，同时避开 lookAt 奇点。
            if (Pitch > 89.0f)
                Pitch = 89.0f;
            // 低于向下 -89° 时钳制在 -89°。
            if (Pitch < -89.0f)
                Pitch = -89.0f;
        }

        // 欧拉角变化后重新计算三个相机方向向量。
        updateCameraVectors();
    }

    // 使用滚轮的竖直偏移调整垂直视野角。
    void ProcessMouseScroll(float yoffset)
    {
        // 向上滚通常为正值，因此减小视野角以获得放大效果。
        Zoom -= yoffset;
        SetZoom(Zoom);
    }

    // 直接设置视野角，供 ImGui 等外部控制面板使用。
    void SetZoom(float zoom)
    {
        // 视野角不得小于 1°，也不得大于 90°。
        Zoom = glm::clamp(zoom, 1.0f, 90.0f);
    }

    // 直接设置偏航角和俯仰角，并立即更新相机方向向量。
    void SetRotation(float yaw, float pitch)
    {
        Yaw = yaw;
        Pitch = glm::clamp(pitch, -89.0f, 89.0f);
        updateCameraVectors();
    }

private:
    // 根据当前偏航角和俯仰角重建相机的正交坐标基。
    void updateCameraVectors()
    {
        // 临时保存由球坐标公式求出的新朝向。
        glm::vec3 front;
        // X 分量由 cos(yaw) 与 cos(pitch) 的乘积得到。
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        // Y 分量直接由 sin(pitch) 得到。
        front.y = sin(glm::radians(Pitch));
        // Z 分量由 sin(yaw) 与 cos(pitch) 的乘积得到。
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        // 单位化朝向，确保各方向移动速度一致。
        Front = glm::normalize(front);
        // Front × WorldUp 得到右向量；单位化后长度固定为 1。
        Right = glm::normalize(glm::cross(Front, WorldUp));
        // Right × Front 得到与另外两轴垂直的相机上向量。
        Up = glm::normalize(glm::cross(Right, Front));
    }
};

// 结束 CAMERA_H 头文件保护条件。
#endif
