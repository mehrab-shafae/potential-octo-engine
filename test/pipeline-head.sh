#!/bin/bash

HOST=127.0.0.1
PORT=8080
NUM_REQUESTS=50   # تعداد درخواست‌های پشت سر هم
BODY_SIZE=10000   # اندازه body هر درخواست (برای تست حافظه)
REQ_PATH="/api/echo"

# ساخت بدنه بزرگ
BODY=$(head -c $BODY_SIZE < /dev/zero | tr '\0' 'x')

# ساخت یک درخواست POST کامل
make_request() {
  echo -en "POST $REQ_PATH HTTP/1.1\r\nHost: $HOST\r\nContent-Type: text/plain\r\nContent-Length: ${#BODY}\r\nConnection: keep-alive\r\n\r\n$BODY"
}

# ارسال چندین درخواست پشت سر هم (pipelining)
{
  for ((i=1; i<=NUM_REQUESTS; i++)); do
    make_request
  done
  # بعد از ارسال همه درخواست‌ها، ۲ ثانیه صبر کن و سپس اتصال را ببند
  sleep 2
} | nc $HOST $PORT | tee responses.txt

echo "تمام پاسخ‌ها در فایل responses.txt ذخیره شد."
