.PHONY: release install clean

BUILD_DIR := build/release
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release

release:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) -j

install:
	cmake --install $(BUILD_DIR)

clean:
	rm -rf build