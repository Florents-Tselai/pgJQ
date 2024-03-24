ARG PG_MAJOR=16
FROM postgres:$PG_MAJOR
ARG PG_MAJOR

COPY . /tmp/pgjq

RUN apt-get update && \
		apt-mark hold locales && \
		apt-get install -y --no-install-recommends build-essential postgresql-server-dev-$PG_MAJOR jq libjq-dev && \
		cd /tmp/pgjq && \
		make clean && \
		make OPTFLAGS="" && \
		make install && \
		mkdir /usr/share/doc/pgjq && \
		cp LICENSE README.md /usr/share/doc/pgjq && \
		rm -r /tmp/pgjq && \
		apt-get remove -y build-essential postgresql-server-dev-$PG_MAJOR && \
		apt-get autoremove -y && \
		apt-mark unhold locales && \
		rm -rf /var/lib/apt/lists/*