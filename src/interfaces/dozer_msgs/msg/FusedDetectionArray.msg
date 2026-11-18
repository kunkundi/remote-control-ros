# One entry per radar object plus unmatched camera detections.
# Only entries with has_3d=true have positions and velocities in header.frame_id (base_link).
# For periodic snapshots, header.stamp is the latest radar acquisition time.
# For image-triggered snapshots, header.stamp is the image acquisition time;
# each radar target was selected from a nearby acquisition time.
std_msgs/Header header
string source_id              # "radar_fusion" for periodic snapshots; "camera:<name>" for one camera
uint64 frame_seq              # Unused for combined snapshots
uint32 image_width            # Unused for combined snapshots
uint32 image_height           # Unused for combined snapshots
FusedDetection[] detections
