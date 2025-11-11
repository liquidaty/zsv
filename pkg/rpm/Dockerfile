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

# https://rpm-software-management.github.io/mock/#setup
# mock needs --privileged to run properly

RUN \
    useradd -m -u 1000 zsv && \
    usermod -a -G mock zsv

USER zsv
WORKDIR /home/zsv

# https://copr.fedorainfracloud.org/api/
RUN mkdir ~/.config
COPY copr ~/.config/

CMD ["bash"]

# docker build -t fedora:zsv .
# docker run -it -v $PWD:/home/zsv --privileged fedora:zsv
