FROM fedora:44

RUN \
    dnf install -y \
        dnf-plugins-core \
        glibc-langpack-en \
        sudo \
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
    useradd -m -s /bin/bash -u 1000 zsv && \
    echo "zsv ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers && \
    usermod -a -G mock zsv

USER zsv
WORKDIR /home/zsv

# https://copr.fedorainfracloud.org/api/
RUN mkdir ~/.config
COPY copr ~/.config/

CMD ["/bin/bash"]

# Docker
# docker build -t fedora:zsv .
# docker run -it --privileged -v $PWD:/home/zsv fedora:zsv

# RPM: https://developer.fedoraproject.org/deployment/rpm/about.html

# COPR: https://developer.fedoraproject.org/deployment/copr/copr-cli.html
# sudo dnf copr enable iamazeem/zsv
# sudo dnf install zsv
# sudo dnf remove zsv
# sudo dnf copr disable iamazeem/zsv
