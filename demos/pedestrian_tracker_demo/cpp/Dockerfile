FROM wahtari/openvino_openmodelzoo_demos:runtime

RUN mkdir /models && cd /models && \
    wget https://storage.openvinotoolkit.org/repositories/open_model_zoo/2022.1/models_bin/3/person-detection-retail-0002/FP32/person-detection-retail-0002.xml && \
    wget https://storage.openvinotoolkit.org/repositories/open_model_zoo/2022.1/models_bin/3/person-detection-retail-0002/FP32/person-detection-retail-0002.bin && \

    wget https://storage.openvinotoolkit.org/repositories/open_model_zoo/2022.1/models_bin/3/person-reidentification-retail-0277/FP32/person-reidentification-retail-0277.xml && \
    wget https://storage.openvinotoolkit.org/repositories/open_model_zoo/2022.1/models_bin/3/person-reidentification-retail-0277/FP32/person-reidentification-retail-0277.xml

COPY pedestrian_tracker_demo /demo

CMD ["/demo", "-no-show", "-i", "alvium", "-at", "ssd", "-m_det", "/models/person-detection-retail-0002.xml", "-m_reid", "/models/person-reidentification-retail-0277.xml"]
