#include <iostream>
#include <cstring>
#include <fstream>
#include <string>

#include "release_utils.h"
#include "config_parser.h"

using namespace std;

#define PATCH_COUNT 100
#define FILE_NAME_LENGTH 150

char    patch_file_list[PATCH_COUNT][FILE_NAME_LENGTH];
int     patch_file_index = 0;

int usage() {
    cout
    << "******************************\n"
    << "*****    release_tool    *****\n"
    << "******************************\n"
    << "[remote]:\t"
    << GIT_PATH
    << "\n[commit]:\t"
    << GIT_VERSION
    << "\n"
    << "\nusage: release_tool -i input_file -b build_hash_file -o output_file [-dir dir]\n"
    << "\n"
    <<  "OPTIONS:\n"
    <<  "   -i         the manifest file which need to be update.\n"
    <<  "   -b         compiled manifest file, generated by `build.sh` or `repo manifest`.\n"
    <<  "   -o         output manifest file, only has hash update from hash_file.\n"
    <<  "   -c         config file, will not update config_file's project.\n"
    <<  "   -dir       use dir's hash(generated by git-merge-base.txt)\n";
    return -1;
}

void listFiles(string rootdirPath) {
    struct dirent * ptr;
    string x,dirPath;
    DIR * dir;

    dir = opendir((char *)rootdirPath.c_str()); //打开一个目录
    while((ptr = readdir(dir)) != NULL) //循环读取目录数据
    {
        if (strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) continue;
        //printf("d_name : %s\n", ptr->d_name);
        x=ptr->d_name;
        dirPath = rootdirPath + "/" + x;
        if (FindKeyWordEndFix(dirPath, ".patch")
            || FindKeyWordEndFix(dirPath, ".diff")
            || FindKeyWordEndFix(dirPath, "vmlinux")
            || FindKeyWordEndFix(dirPath, ".config")
            || FindKeyWordEndFix(dirPath, ".txt")) {
                if (FindKeyWordEndFix(dirPath, "git-merge-base.txt")) {
                    strcpy(patch_file_list[patch_file_index], dirPath.c_str()); //存储到数组
                    patch_file_index++;
                    //cout << dirPath << patch_file_index << endl;
                }
                continue;
            } else {
                listFiles(dirPath);
            }
    }
    closedir(dir);//关闭目录指针
}

int inPatchedList(string patch_path, string fixed_dir) {
    int p = 0;
    for (; p < patch_file_index; p++) {
        string s(patch_file_list[p]);
        string patched_path;
    //    cout << "s:" << s << ","<< fixed_dir << endl;
        ReplaceString(fixed_dir + "/", &s, "");
        ReplaceString("/git-merge-base.txt", &s, "");
    //    cout << s << "|||" << patch_path << endl;
        if (strcmp(s.data(), patch_path.data()) == 0) return p;
    }
    return -1;
}

int inConfigList(string projectName, string config_file) {
    int p = 0;
    vector<ParserPoint> array = openConfigFile(config_file);
    for (; p < array.size(); p++) {
        if (array[p].getPointName() == "Filter" &&
            array[p].contains(projectName)) {
                return 0;
            }
    }
    return -1;
}

void update(string input_file, string hash_file, string output_file, bool fixed_manifest, string fixed_dir, bool use_config, string config_file) {
    ifstream fin(input_file.data());
    ofstream fout;
    fout.open(output_file.data(), ios::trunc);
    string tempStr;
    while (getline(fin, tempStr)) {
        string result;
        // line filter, only change project line
        if (HasKeyWordInString(tempStr, "<project")) {
            //find, convert str and write
            LogD("Need update line:" + tempStr);
            string node_name;
            string node_path;
            bool foundPath = true;
            //find 'path/name' in string line
            if (!FindKeyName(tempStr, "path=\"", "\"", &node_path)) {
                // not fount path = xxx, actully path = name
                //LogD("%s not found `path`, path = name", input_file);
                if (FindKeyName(tempStr, "name=\"", "\"", &node_name)) {
                    node_path = node_name;
                    LogD("use name instead of path");
                } else {
                    LogE("your manifest file missing path & name, please check!");
                }
                foundPath = false;
            }
            //cout << node_path << "," << node_name << endl;
            //start update hash.
            if (use_config && inConfigList(node_path, config_file) == 0) {
                cout << "Keep origin:" << node_path << endl;
                result = tempStr;
                goto WRITE_FILE;
            }

            if (fixed_manifest && inPatchedList(node_path, fixed_dir) >= 0) {
                // update hash from patch dir.
                int patch_pos = inPatchedList(node_path, fixed_dir);
                string hashFromGitMergeBase;
                string originHash;
                //cout << "node:" << node_path << endl;
                if(FindHashFromFile(patch_file_list[patch_pos], &hashFromGitMergeBase) && FindHashFromLine(tempStr, &originHash)) {
                    //write
                    ReplaceString(originHash, &tempStr, hashFromGitMergeBase);
                    result = tempStr;
                    goto WRITE_FILE;
                } else {
                    // can't find commit id from git-merge-base.txt
                }
            } else {
                // update hash from hash file.
                string hash;
                //find 'hash' of string
                if (FindHashOfKeyName(hash_file, node_path, &hash, foundPath)) {
                    //found new hash, replace origin hash.
                    LogD("find HASH for project!");
                    string originHash;
                    if (FindHashFromLine(tempStr, &originHash)) {
                        //do update Hash
                        ReplaceString(originHash, &tempStr, hash);
                        //do update upstream
                        string upstream;
                        if (FindUpstreamOfKeyName(hash_file, node_path, &upstream, foundPath)) {
                            LogD("find upstream for project!");
                            string originUpstream;
                            if (FindKeyName(tempStr, "upstream=\"", "\"", &originUpstream)) {
                                ReplaceString(originUpstream, &tempStr, upstream);
                            }
                        }
                    } else {
                        // can't find hash for origin line.
                        LogE(input_file + " missing hash at line: " + node_name);
                    }
                } else {
                    //no found, replace with npi
                    //ReplaceString("aosp", &tempStr, "hash_no_found");
                    LogE(hash_file + " missing hash at line: " + node_path);
                }
                result = tempStr;
                goto WRITE_FILE;
            }
        } else if (HasKeyWordInString(tempStr, "<default")) {
            // update default revision
            string revisionDefault, revisionNew;
            FindKeyName(tempStr, "revision=\"", "\"", &revisionDefault);
            FindDefaultRevision(hash_file, &revisionNew);
            ReplaceString(revisionDefault, &tempStr, revisionNew);
            result = tempStr;
        } else {
            //keep origin string
            result = tempStr;
            goto WRITE_FILE;
        }

WRITE_FILE:
        fout << result << endl;
        result.clear();
    }
    fout.close();
}

int main(int argc, char** argv) {

    if (argc == 1) return usage();

    string  input_file;
    string  hash_file;
    string  output_file;
    string  config_file;
    string  fixed_dir;
    bool    fixed_manifest = false;
    bool    use_config = false;
    int     i = 1;

    // Parse flags, all of which start with '-'
    for ( ; i < argc; ++i) {
        const size_t len = strlen(argv[i]);
        const char *s = argv[i];
        if (len < 2 || s[1] == 'h') {
            return usage();
        }
        if (s[0] != '-') {
            continue;  // On to the positional arguments.
        }
        string the_rest = argv[i+1];
        if (s[1] == 'i') {
            input_file = the_rest;
        } else if (s[1] == 'b') {
            hash_file = the_rest;
        } else if (s[1] == 'c') {
            use_config = true;
            config_file = the_rest;
        } else if (s[1] == 'o') {
            output_file = the_rest;
        } else if (strcmp(s, "-dir") == 0) {
            //strcpy(output_file, the_rest);
            fixed_manifest = true;
            fixed_dir = getPath(the_rest);
            listFiles(fixed_dir);
        } else {
            cout << "Invalid argument '" << s << "'." << endl;
            return usage();
        }
    }
    if (fixed_manifest) {
        //cout << "fixed_manifest " << fixed_dir << endl;
        cout << "=========== Use these patches to fixed manifest! ==============" << endl;
    }
    // Update Hash with these parameters.
    update(input_file, hash_file, output_file, fixed_manifest, fixed_dir, use_config, config_file);

    cout << "======= Check your output ========" << endl;
    cout << "\t" << output_file << endl;
    cout << "============= Done! ==============" << endl;
    return 0;
}
