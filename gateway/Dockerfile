FROM python:3.12-slim

LABEL org.opencontainers.image.source=https://github.com/SebastienStDenis/smart-grind-by-weight
LABEL org.opencontainers.image.description="MTA subway arrivals gateway for the grinder's trains screensaver"

WORKDIR /srv
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app app
COPY static static

VOLUME /data
EXPOSE 8600
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8600"]
