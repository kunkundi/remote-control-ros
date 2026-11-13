uint32 class_id
# class_id == 4294967295 denotes unknown (no camera match).
float32 score
float32 x
float32 y
float32 w
float32 h
# 2D fields are normalized in camera_source_id's image; zero when unknown.
bool has_track_id
uint32 track_id
bool has_3d
string camera_source_id       # Empty when unknown.
string radar_source_id        # Radar name; empty for camera-only detections.
uint8 radar_sensor_id
uint8 radar_object_id
float32 position_x
float32 position_y
float32 position_z
float32 velocity_x
float32 velocity_y
float32 velocity_z
float32 radar_rcs
