FROM archlinux:latest

RUN pacman -Syu --noconfirm && pacman -Sy --noconfirm \
   clang \
   cmake \
   gcc \
   make

WORKDIR /opt

CMD [ "bash" ]
