import os
import sys
import time
import urllib.request
import multiprocessing

# Set env overrides to ensure mock mode is used and it doesn't try to open COM10
os.environ["SERIAL_MOCK"] = "true"
os.environ["CLOUD_MODE"] = "false"

def run_server():
    import uvicorn
    # Add backend path to sys.path
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
    uvicorn.run("main:app", host="127.0.0.1", port=8001, log_level="info")

if __name__ == "__main__":
    p = multiprocessing.Process(target=run_server)
    p.start()
    
    # Wait for server to start
    time.sleep(3)
    
    print("\n--- Testing HTTP connection to local mock server ---")
    urls = [
        "http://127.0.0.1:8001/",
        "http://127.0.0.1:8001/display/",
        "http://127.0.0.1:8001/display/index.html",
        "http://127.0.0.1:8001/display/js/app.js",
        "http://127.0.0.1:8001/api/state"
    ]
    
    success = True
    for url in urls:
        try:
            req = urllib.request.Request(url)
            with urllib.request.urlopen(req, timeout=3) as response:
                code = response.getcode()
                content = response.read()[:150]
                print(f"[OK] {url} -> HTTP {code}")
                print(f"     Content: {content}\n")
        except Exception as e:
            print(f"[FAIL] {url} -> {e}\n")
            success = False
            
    # Terminate process
    p.terminate()
    p.join()
    
    if success:
        print("ALL ENDPOINTS WORKED PERFECTLY!")
        sys.exit(0)
    else:
        print("SOME ENDPOINTS FAILED!")
        sys.exit(1)
