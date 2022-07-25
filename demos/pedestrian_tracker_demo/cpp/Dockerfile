FROM wahtari/openvino_openmodelzoo_demos:runtime

RUN mkdir /models && cd /models && \
    wget https://storage.openvinotoolkit.org/repositories/open_model_zoo/2022.1/models_bin/3/pedestrian-detection-adas-0002/FP32/pedestrian-detection-adas-0002.xml && \
    wget https://storage.openvinotoolkit.org/repositories/open_model_zoo/2022.1/models_bin/3/pedestrian-detection-adas-0002/FP32/pedestrian-detection-adas-0002.bin

COPY pedestrian_tracker_demo /demo

CMD ["/demo", "-no-show", "-at", "openpose", "-i", "/models/pedestrian-detection-adas-0002.bin", "-m", "/models/pedestrian-detection-adas-0002.xml"]