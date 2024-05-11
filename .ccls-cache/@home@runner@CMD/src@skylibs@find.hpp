#pragma once

#include <algorithms>

namespace sky {
	template <typename T>
	
	int find(std::vector<T> vec, T value) {
		std::vector<T>::iterator it = std::find(vec.begin(), vec.end(), value);

		if(it != vec.end()) {
			return it - vec.begin();
		} else {
			return -1;
		}
	}

	template <typename T>
	
	void eraseAt(std::vector<T>& vec, int at) {
		vec.erase(vec.begin()+at)
	}
}
