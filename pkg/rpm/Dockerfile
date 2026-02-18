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
        perl \
        rpmdevtools \
        fedora-packager \
        fedora-packager-kerberos \
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
# docker run -it --privileged -v $HOME/.ssh:/home/zsv/.ssh:ro -v $PWD:/home/zsv fedora:zsv
# chown -R $(whoami) ~/.ssh

# RPM: https://developer.fedoraproject.org/deployment/rpm/about.html

# COPR: https://developer.fedoraproject.org/deployment/copr/copr-cli.html
# 
# dnf
# sudo dnf install -y dnf-plugins-core
# sudo dnf copr enable iamazeem/zsv
# sudo dnf install -y zsv
# sudo dnf remove -y zsv
# sudo dnf copr disable iamazeem/zsv
# 
# yum
# sudo yum install -y yum-plugin-copr
# sudo yum copr enable iamazeem/zsv
# sudo yum install -y zsv
# sudo yum remove -y zsv
# sudo yum copr disable iamazeem/zsv

# KOJI: https://docs.fedoraproject.org/en-US/package-maintainers/Using_the_Koji_Build_System/
# fkinit -u USERNAME
# koji build --scratch <rawhide | f44 | f43 | f42 | epel10> SRPMS/zsv-1.1.0-1.fc44.src.rpm
