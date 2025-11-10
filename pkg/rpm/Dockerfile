FROM fedora:44

RUN \
    dnf install -y \
        glibc-langpack-en \
        git \
        gcc \
        man \
        tree \
        rpmdevtools \
        fedora-packager \
        copr-cli \
        mock \
        ncurses-devel \
        ncurses \
    && dnf clean all

USER root
WORKDIR /root

# https://copr.fedorainfracloud.org/api/
COPY copr ~/.config/copr

CMD ["bash"]
