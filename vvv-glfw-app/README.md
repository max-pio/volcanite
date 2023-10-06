# VVV GLFW Application

A simple interactive rendering window using [Dear ImGUI](https://github.com/ocornut/imgui) for parameter editing.

## Camera Controls

Camera movement works in one of two ways. See `Application::updateCamera()` for details:
* The first option is a classic first person camera. Use `WSAD` for movement.
`E` and `Q` can be used for vertical movement.
Press and hold the left mouse button to rotate the camera around the y-axis.
Press `SHIFT` to speed up or `CTRL` to slow down the movement.
* The second option is an orbit camera. 
Press and hold a mouse button to rotate the camera around the world space center.
`SHIFT` and `CTRL` lock the rotation to a single axis.
The mouse wheel controls the distance to the rotation center.
Pressing `R` performs a constant rotation around the y-axis.

Hitting `F9` records the camera pose and frame time of each following frame until it is pressed again.
Both files are stored in the user home directory in a subfolder `vvv_video`.
The record can be replayed by hitting `F10`.
`F11` replays the record and outputs a PNG image for each frame that can later be concatenated to a video using an external program like ffmpeg:
```
ffmpeg -f concat -safe 0 -i ~/vvv_video/video_timing.txt ~/vvv_video/video.mp4
```

## Renderer Integration

The application performs a rendering loop and blits the most recent output of a specified renderer to the screen.
The renderer output resolution does not have to match the window resolution as the output is scaled to fit into the window.
You can hot reload shaders by pressing `F5`.
The `GuiInterface` abstraction in the vvv library is used to expose renderer parameters to the user.
In case of the GLFW Application, one ImGUI window is created per parameter group.
