# dozer_vehicle

车辆控制节点，安全相关模块。负责人：待定。

- 输入：`/vehicle/drive_cmd`（DriveCommand）、服务 `/vehicle/set_gear`（SetGear）
- 输出：`/vehicle/state`（VehicleState，10 Hz）、`/diagnostics`
- 来源：由 rtc-framework `client/modules/vehicle/` 迁移，序号校验、范围校验与 300 ms 看门狗逻辑保持不变。
- 看门狗在本节点内按收到有效指令的时间判定，超时执行本地安全停车。
- 任何修改须附带测试并经过安全评审，见工作空间 README 第 10 节。
