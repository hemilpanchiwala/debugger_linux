#include <iostream>
#include <bits/stdc++.h>

using namespace std;

vector<string> split(const string &s, char delimiter) {

    vector<string> split_string{};
    stringstream ss {s};
    string item;

    while(getline(ss, item, delimiter)) {
        split_string.push_back(item);
    }

    return split_string;

}

bool is_prefix(const string& a, const string& b){

    if (a.size() > b.size()) {
        return false;
    }

    return equal(a.begin(), a.end(), b.begin());

}

bool is_suffix(const string& a, const string& b) {
    
    if(a.size() > b.size()) {
        return false;
    }

    return equal(a.begin(), a.end(), b.begin() + (b.size() - a.size()));

}
