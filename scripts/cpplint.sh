pip install -U cpplint
cpplint --repository=. --recursive --linelength=120 --verbose=4 --filter=-build/include,-build/header_guard ./src