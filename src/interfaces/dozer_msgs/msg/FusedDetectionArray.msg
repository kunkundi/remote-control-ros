# One entry per radar object. Positions and velocities are in header.frame_id (base_link).
# header.stamp is the latest radar acquisition time used for this fused snapshot.
std_msgs/Header header
string source_id              # "radar_fusion" for the combined radar snapshot
uint64 frame_seq              # Unused for combined snapshots
uint32 image_width            # Unused for combined snapshots
uint32 image_height           # Unused for combined snapshots
FusedDetection[] detections
