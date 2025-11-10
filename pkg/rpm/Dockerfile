FROM fedora:44

RUN \
    dnf install -y \
        git \
        gcc \
        rpmdevtools \
        fedora-packager \
        copr-cli \
        mock \
        ncurses-devel \
        jq-devel \
        pcre2-devel \
    && dnf clean all

RUN dnf install -y glibc-langpack-en

RUN dnf remove -y jq-devel pcre2-devel

USER root
WORKDIR /root
COPY copr ~/.config/copr

CMD ["bash"]
