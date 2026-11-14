FROM ubuntu:24.04

ENV PYTHONUNBUFFERED=1 PYTHONPATH=/opt/sgpl-static

# clang and opt 18; MPI and METIS headers, which conduit's headers include.
# CPATH puts MPI's headers on the compiler's default include path, where
# METIS's already are.
ENV CPATH=/opt/mpi-include
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates clang-18 git libmetis-dev libopenmpi-dev llvm-18 \
        python3 python3-tqdm \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && ln -s /usr/bin/opt-18 /usr/local/bin/opt \
    && ln -s /usr/lib/$(uname -m)-linux-gnu/openmpi/include /opt/mpi-include

COPY include /opt/sgpl-static/include
COPY source /opt/sgpl-static/source
COPY sgpl_static /opt/sgpl-static/sgpl_static

ENTRYPOINT ["python3", "-m", "sgpl_static"]
