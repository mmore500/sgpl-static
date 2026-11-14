FROM ubuntu:24.04

ENV PYTHONUNBUFFERED=1 PYTHONPATH=/opt/sgpl-static

# clang and opt 18; MPI and METIS headers, which conduit's headers include
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates clang-18 git libmetis-dev libopenmpi-dev llvm-18 \
        python3 \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && ln -s /usr/bin/opt-18 /usr/local/bin/opt

COPY cpp /opt/sgpl-static/cpp
COPY sgpl_static /opt/sgpl-static/sgpl_static

ENTRYPOINT ["python3", "-m", "sgpl_static"]
