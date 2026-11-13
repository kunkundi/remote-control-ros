"""Batch-wide NMS for the fixed-size YOLO TensorRT detection engine."""

import torch
import torchvision
from ultralytics.engine.results import Results
from ultralytics.models.yolo.detect.predict import DetectionPredictor
from ultralytics.utils.ops import scale_boxes, xywh2xyxy


class BatchedDetectionPredictor(DetectionPredictor):
    def postprocess(self, preds, img, orig_imgs, **kwargs):
        prediction = preds[0] if isinstance(preds, (list, tuple)) else preds
        if (not isinstance(prediction, torch.Tensor) or prediction.ndim != 3
                or prediction.shape[-1] == 6 or getattr(self.model, 'end2end', False)
                or self.args.classes is not None or self.args.agnostic_nms):
            return super().postprocess(preds, img, orig_imgs, **kwargs)

        batch_size, channels, anchors = prediction.shape
        class_count = channels - 4
        scores, classes = prediction[:, 4:].max(dim=1)
        candidate = scores > self.args.conf
        image_indices = torch.arange(batch_size, device=prediction.device)[:, None]
        image_indices = image_indices.expand(batch_size, anchors)[candidate]
        boxes = xywh2xyxy(prediction[:, :4].transpose(1, 2))[candidate]
        scores = scores[candidate]
        classes = classes[candidate]

        output = [prediction.new_zeros((0, 6)) for _ in range(batch_size)]
        if boxes.numel():
            # Spatial offsets separate both classes and cameras in one CUDA NMS call.
            offsets = (classes + image_indices * class_count).to(boxes.dtype) * 7680
            keep = torchvision.ops.nms(boxes + offsets[:, None], scores,
                                       kwargs.get('iou', self.args.iou))
            for i in range(batch_size):
                chosen = keep[image_indices[keep] == i][:self.args.max_det]
                output[i] = torch.cat((boxes[chosen], scores[chosen, None],
                                       classes[chosen, None].to(boxes.dtype)), dim=1)
        if isinstance(orig_imgs, list) and len({im.shape for im in orig_imgs}) == 1:
            # All six cameras use the same image size; scale coordinates in one GPU call.
            lengths = [len(detections) for detections in output]
            combined = torch.cat(output)
            combined[:, :4] = scale_boxes(img.shape[2:], combined[:, :4],
                                           orig_imgs[0].shape)
            return [Results(orig, path=path, names=self.model.names, boxes=detections)
                    for orig, path, detections in zip(
                        orig_imgs, self.batch[0], combined.split(lengths))]
        return self.construct_results(output, img, orig_imgs)
