# dozer_bringup

启动编排与整车配置。

- `launch/dozer.launch.py`：总入口，`use_mock_radar:=true` 时启动模拟雷达
- `launch/static_tf.launch.py`：按 `config/extrinsics.yaml` 发布传感器外参
- `config/*.yaml`：各模块参数（radar、camera 等），统一在此维护
- `systemd/`：开机自启服务（后续添加）
