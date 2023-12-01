#pragma once

#include <numbers>

#include <functional>
#include <vvv/core/preamble_forward_decls.hpp>

namespace vvv {

/*! Holds state for a first person camera that characterizes the world to
projection space transform completely, except for the aspect ratio. It also
provides enough information to update the camera interactively. It does not
store any transforms or other redundant information. Such information has
to be computed as needed.

 Our world and camera setup uses a right-handed coordinate system (y is up, x to the right, z pointing out of the plane spanned by xy)
 */
class Camera {
public:
    //! If true, this is an orbital (rotate with mouse + scrollwheel) camera instead of WASDQE + Mouse rotation
    bool orbital;
    //! The distance of the camera to (0,0,0) if in orbital mode
    float orbital_radius;
    //! The position of the camera in world space
    glm::vec3 position_world_space;
    //! The rotation of the camera around the global y-axis in radians
    float rotation_y;
    //! The rotation of the camera around the local x-axis in radians. Without
    //! rotation the camera looks into the negative z-direction.
    float rotation_x;
    //! The vertical field of view (top to bottom) in radians
    float vertical_fov;
    //! The distance of the near plane and the far plane to the camera position
    float near, far;
    //! The default speed of this camera in meters per second when it moves
    //! along a single axis
    float speed;
    //! 1 iff mouse movements are currently used to rotate the camera
    bool rotate_camera;
    //! The rotation that the camera would have if the mouse cursor were moved
    //! to coordinate (0, 0) with rotate_camera enabled
    float rotation_x_0, rotation_y_0;
    //! The projection mode of the camera
    enum class Mode { Perspective, Orthogonal };
    Mode camera_mode;
    float orthogonal_scale;

    Camera(bool is_orbital = false) : orbital(is_orbital), rotation_x(0), rotation_y(0), rotation_x_0(0), rotation_y_0(0), near(0.05f), far(1.0e3f), vertical_fov(0.33f * std::numbers::pi),
                                      speed(2.0f), position_world_space(0, 0, 5), rotate_camera(false), camera_mode(Mode::Perspective), orthogonal_scale(5.f) {
        reset();
    }


    //! Constructs the world to view space transform for the given camera
    glm::mat4 get_world_to_view_space() const;

    //! Constructs the view to projection space transform for the given camera and
    //! the given width / height ratio
    glm::mat4 get_view_to_projection_space(float aspect_ratio) const;
    glm::mat4 get_view_to_projection_space(const vk::Extent2D extent) const {
        return get_view_to_projection_space(getAspectRatio(extent));
    }

    //! Constructs the world to projection space transform for the given camera and
    //! the given width / height ratio
    glm::mat4 get_world_to_projection_space(float aspect_ratio) const;
    glm::mat4 get_world_to_projection_space(const vk::Extent2D extent) const {
        return get_world_to_projection_space(getAspectRatio(extent));
    }

    void reset() {
        rotation_x = 0.f;
        rotation_y = 0.f;
        rotation_x_0 = 0.f;
        rotation_y_0 = 0.f;
        orbital_radius = 2.f;
        speed = 2.0f;
        position_world_space = glm::vec3(0, 0, 2);
        rotate_camera = false;
        camera_mode = Mode::Perspective;
        orthogonal_scale = 5.0f;
    }

    static inline float getAspectRatio(const vk::Extent2D extent) { return ((float)extent.width) / ((float)extent.height); }

    /**
     * Register a function that is called whenever the camera is moved or rotated.
     * Overrides any previously defined callback function.
     * There is no callback function defined initially.
     *
     * @param callbackFunction function that is called on camera updates, may be nullptr to remove the current callback function
     */
    void registerCameraUpdateCallback(std::function<void()> callbackFunction);

    void onCameraUpdate();

    void writeTo(std::ostream& out, bool human_readable=false) {
        if(human_readable) {
            out << "position: " << position_world_space.x << " " << position_world_space.y << " " << position_world_space.z << std::endl;
            out << "rotation: " << rotation_x << " " << rotation_y << std::endl;
        } else {
            out.write(reinterpret_cast<char *>(&rotation_x), sizeof(rotation_x));
            out.write(reinterpret_cast<char *>(&rotation_y), sizeof(rotation_y));
            out.write(reinterpret_cast<char *>(&position_world_space), sizeof(position_world_space));
        }
    }
    void readFrom(std::istream& in, bool human_readable=false) {
        if(human_readable) {
            std::string tmp;
            in >> tmp; // "position:"
            in >> position_world_space.x;
            in >> position_world_space.y;
            in >> position_world_space.z;
            in >> tmp; // "rotation:"
            in >> rotation_x;
            in >> rotation_y;
        } else {
            in.read(reinterpret_cast<char *>(&rotation_x), sizeof(rotation_x));
            in.read(reinterpret_cast<char *>(&rotation_y), sizeof(rotation_y));
            in.read(reinterpret_cast<char *>(&position_world_space), sizeof(position_world_space));
        }
    }

private:
    std::function<void()> m_cameraUpdateFunction = nullptr;
};

}
