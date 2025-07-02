#!/bin/bash

HOST=127.0.0.1
PORT=8080
NUM_REQUESTS=200   # تعداد درخواست‌های پشت سر هم (بار سنگین‌تر)
BODY_SIZE=50000    # اندازه body هر درخواست (بار سنگین‌تر)
REQ_PATH="/api/echo"

# ساخت بدنه بزرگ
BODY=$(head -c $BODY_SIZE < /dev/zero | tr '\0' 'x')

# ساخت یک درخواست POST کامل
make_request() {
  echo -en "POST $REQ_PATH HTTP/1.1\r\nHost: $HOST\r\nContent-Type: text/plain\r\nContent-Length: ${#BODY}\r\nConnection: keep-alive\r\n\r\n$BODY"
}

# زمان شروع
START=$(date +%s)

# ارسال چندین درخواست پشت سر هم (pipelining)
{
  for ((i=1; i<=NUM_REQUESTS; i++)); do
    make_request
  done
  # ورودی را می‌بندیم تا سرور بفهمد داده‌ای دیگر نمی‌آید
} | nc $HOST $PORT > /dev/null

# زمان پایان
END=$(date +%s)
DURATION=$((END - START))

echo "تست بارگذاری سنگین با $NUM_REQUESTS درخواست و body $BODY_SIZE بایت انجام شد."
echo "مدت زمان اجرا: $DURATION ثانیه"

# اگر خواستی تعداد پاسخ‌های دریافتی را هم بشماری (اختیاری):
{
  for ((i=1; i<=NUM_REQUESTS; i++)); do
    make_request
  done
} | nc $HOST $PORT | grep -c 'HTTP/1.1 200 OK'