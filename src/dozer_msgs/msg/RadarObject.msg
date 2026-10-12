# 单个毫米波雷达目标，对应 SR73F 0x60B Obj_1_General。
# 坐标位于所属 RadarObjectArray 的 header.frame_id（雷达自身坐标系，REP-103：x 前、y 左、z 上）。
# 注意：SR73F 横向距离的正方向需实测确认，驱动负责转换为 y 左为正。

uint8 id            # 雷达内部目标 ID（0-255），跨周期可用于关联
float32 x           # 纵向距离 m（DistLong）
float32 y           # 横向距离 m（DistLat，已转换为左正）
float32 vx          # 纵向相对速度 m/s（VrelLong）
float32 vy          # 横向相对速度 m/s（VrelLat，已转换为左正）
float32 rcs         # 雷达散射截面 dBsm
uint8 dyn_prop      # 动态属性原始值；SR73F 当前固件未开放，恒为 0
