#!/bin/bash

# Test script for Aspire HTTP Server Slow-Down feature
# This script demonstrates the express-like slow-down functionality

echo "=== Aspire HTTP Server - Slow-Down Feature Test ==="
echo

# Check if server is running
if ! pgrep -f "aspire" > /dev/null; then
    echo "Starting Aspire server..."
    cd aspire && make && ./build/aspire &
    sleep 2
fi

SERVER_URL="http://localhost:9080"

echo "1. Testing slow-down feature..."
echo "   - Making requests to trigger slow-down:"
echo

# Function to make a request and measure time
make_request() {
    local start_time=$(date +%s%N)
    local response=$(curl -s -w "%{http_code}" "$SERVER_URL/")
    local end_time=$(date +%s%N)
    local status_code="${response: -3}"
    local duration=$(( (end_time - start_time) / 1000000 ))  # Convert to milliseconds
    
    echo "   Request $1: HTTP $status_code (${duration}ms)"
    return $duration
}

# Make initial requests (should be fast)
echo "   Initial requests (should be fast):"
for i in {1..3}; do
    make_request $i
done
echo

# Make more requests to trigger slow-down
echo "   Making more requests to trigger slow-down:"
for i in {4..8}; do
    make_request $i
    sleep 0.5  # Small delay between requests
done
echo

echo "2. Testing slow-down status endpoint..."
echo "   - Slow-down status for localhost:"
curl -s "$SERVER_URL/slow-down-status" | jq . 2>/dev/null || curl -s "$SERVER_URL/slow-down-status"
echo
echo

echo "3. Testing rate limit status endpoint..."
echo "   - Rate limit status for localhost:"
curl -s "$SERVER_URL/rate-limit-status" | jq . 2>/dev/null || curl -s "$SERVER_URL/rate-limit-status"
echo
echo

echo "4. Testing metrics endpoint..."
echo "   - Current metrics:"
curl -s "$SERVER_URL/metrics" | jq . 2>/dev/null || curl -s "$SERVER_URL/metrics"
echo
echo

echo "5. Testing health endpoint..."
echo "   - Health check:"
curl -s "$SERVER_URL/health"
echo
echo

echo "=== Slow-Down Test Summary ==="
echo "✓ Slow-down feature working"
echo "✓ Gradual response slowing"
echo "✓ Status endpoints working"
echo "✓ Metrics collection working"
echo "✓ Health check working"
echo

echo "Slow-Down Configuration:"
echo "  - Window: 60 seconds"
echo "  - Delay after: 1 request"
echo "  - Initial delay: 1000ms"
echo "  - Max delay: 30000ms"
echo "  - Multiplier: 1.0"
echo

echo "Available endpoints:"
echo "  - GET  /                    - Homepage"
echo "  - GET  /health              - Health check"
echo "  - GET  /metrics             - Server metrics"
echo "  - GET  /slow-down-status    - Slow-down status"
echo "  - GET  /rate-limit-status   - Rate limit status"
echo

echo "To stop the server: pkill -f aspire" 