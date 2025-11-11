FROM fedora:44

RUN \
    dnf install -y \
        dnf-plugins-core \
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

RUN useradd -m -u 1000 zsv
USER zsv
WORKDIR /home/zsv

# https://copr.fedorainfracloud.org/api/
RUN mkdir ~/.config
COPY copr ~/.config/

CMD ["bash"]
