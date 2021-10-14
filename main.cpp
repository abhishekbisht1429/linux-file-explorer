#include<iostream>
#include<vector>
#include<string>
#include<stack>
#include<iomanip>

#include<termios.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<errno.h>
#include<signal.h>
#include<stdlib.h>
#include<string.h>
#include<dirent.h>
#include<sys/types.h>
#include<sys/stat.h>

using namespace std;

#define TRUE 1
#define FALSE 0

#define NORMAL_MODE 0
#define COMMAND_MODE 1

#define MOVE(x, y) cout<<"\033["<<x<<";"<<y<<"H"<<flush
#define CLEAR() cout<<"\033[H"<<"\033[J"<<flush
#define SAVE_CURSOR() cout<<"\033[s"<<flush
#define RESTORE_CURSOR() cout<<"\033[u"<<flush
#define CLEAR_LINE() cout<<"\033[K"<<flush
#define MOVE_CURSOR_UP() cout<<"\033[1A"<<flush;
#define MOVE_CURSOR_DOWN() cout<<"\033[1B"<<flush;
// #define DISABLE_LINE_WRAP() cout<<"\033[7l"<<flush;

struct cursor_pos {
    int i, j;
};

struct entry {
    string name;
    string size;
    string owner;
    string permission;
    string last_mod;
};

/*
    Global Configuration Variables
*/
struct termios tty_config;
struct winsize tty_ws;
const char *tty = "/dev/tty";
/* file descriptor for tty */
int fd;


/*
    Global Variables specific to application
*/
/* application home */
string home;
/* cursor position */
cursor_pos cpos;
/* status bar start position */
cursor_pos st_start;
/* index of first file to be displayed */
int lo;
/* index of last file to be displayed */
int hi;
/* top row number of display area */
int top;
/* bottom row number of display area */
int bottom;
/* current working directory */
string cwd;
/* array to store enteries of files in current dir */
vector<entry> enteries;
/* current mode */
int mode;
/* current status */
string status;
/* stack to store history */
stack<string> history;
/* stack to store path of left right history */
stack<string> lr_history;


/*
    Normal mode config variables
*/
int selected_entry;

/*
    functions dealing with content rendering
*/
void render_status() {
    SAVE_CURSOR();
    MOVE(st_start.i, 1);
    CLEAR_LINE();
    cout<<status;
    RESTORE_CURSOR();
}

void set_status(string sts, bool render = false) {
    status = sts;
    if(render)
        render_status();
}

void render() {
    /* clear screen */
    CLEAR();

    vector<int> max_col_widths(5);
    for(int i=lo; i<=hi; ++i) {
        max_col_widths[0] = max(max_col_widths[0], (int)enteries[i].name.size());
        max_col_widths[1] = max(max_col_widths[1], (int)enteries[i].size.size());
        max_col_widths[2] = max(max_col_widths[2], (int)enteries[i].owner.size());
        max_col_widths[3] = max(max_col_widths[3], (int)enteries[i].permission.size());
        max_col_widths[4] = max(max_col_widths[4], (int)enteries[i].last_mod.size());
    }
    /* render content */
    for(int i=lo; i<=hi; ++i) {
        // cout<<i<<"\n";
        cout<<left<<setw(max_col_widths[0])<<enteries[i].name<<"\t";
        cout<<left<<setw(max_col_widths[1])<<enteries[i].size<<"\t";
        cout<<left<<setw(max_col_widths[2])<<enteries[i].owner<<"\t";
        cout<<left<<setw(max_col_widths[3])<<enteries[i].permission<<"\t";
        cout<<left<<setw(max_col_widths[4])<<enteries[i].last_mod;
        if(i<hi) cout<<"\n";
    }
    cout<<flush;

    /* print status */
    render_status();

    /* MOVE cursor to correct pos */
    MOVE(selected_entry-lo+1, 1);
}

/* store all info about files of current 
    directory in enteries array */
void list() {
    DIR *temp = opendir(cwd.c_str());
    if(temp == NULL) {
        printf("unable to open current directory\n");
        return;
    }
    enteries.clear();

    dirent *ent;
    while((ent = readdir(temp)) != NULL) {
        string fname = ent->d_name;
        string abs_path = cwd + "/" + fname;
        
        struct stat st;
        stat(abs_path.c_str(), &st);

        entry e;
        e.name = fname;
        e.size = to_string(st.st_size);
        e.owner = to_string(st.st_uid);
        /* extract file type and permisison info */
        if(S_ISREG(st.st_mode)) e.permission += "-";
        else if(S_ISBLK(st.st_mode)) e.permission += "b";
        else if(S_ISCHR(st.st_mode)) e.permission += "c";
        else if(S_ISDIR(st.st_mode)) e.permission += "d";
        else if(S_ISFIFO(st.st_mode)) e.permission += "p";
        else if(S_ISLNK(st.st_mode)) e.permission += "l";
        else if(S_ISSOCK(st.st_mode)) e.permission += "s";

        e.permission += (st.st_mode & S_IRUSR)?'r':'-';
        e.permission += (st.st_mode & S_IWUSR)?'w':'-';
        e.permission += (st.st_mode & S_IXUSR)?'x':'-';

        e.permission += (st.st_mode & S_IRGRP)?'r':'-';
        e.permission += (st.st_mode & S_IWGRP)?'w':'-';
        e.permission += (st.st_mode & S_IXGRP)?'x':'-';

        e.permission += (st.st_mode & S_IROTH)?'r':'-';
        e.permission += (st.st_mode & S_IWOTH)?'w':'-';
        e.permission += (st.st_mode & S_IXOTH)?'x':'-';
        /* extract last modified time */
        char buf[100];
        strftime(buf, sizeof buf, "%e %b %Y %H:%M:%S", localtime(&st.st_atim.tv_sec));
        e.last_mod += string(buf, strlen(buf));
        enteries.push_back(e);
    }
    // cout<<enteries.size()<<"\n";
}

/* Handle tty window resize */
void signal_win_resize(int signum) {
    if(ioctl(fd, TIOCGWINSZ, &tty_ws) < 0) {
        fprintf(stderr, "failed to obtain new size after window resize\n");
        exit(1);
    }

    /* re calculate status bar position */
    st_start.i = tty_ws.ws_row;
    st_start.j = 1;

    /* recalculate display area */
    bottom = tty_ws.ws_row - 1;

    // set_status(to_string(tty_ws.ws_row)+" "+to_string(tty_ws.ws_col)+"\t"+to_string(lo)+" "+to_string(hi));

    /* adjust lo and hi */
    if(mode == NORMAL_MODE) {
        /* if enteries display window is greater than display window then decrease it */
        while((hi - lo + 1) > (bottom-top+1)) {
            if(hi > selected_entry) --hi;
            else if(lo < selected_entry) ++lo; 
        }
        /* if enteries display window is less than display window then increase it */
        while((hi - lo + 1) < (bottom-top+1) && (hi - lo + 1) < enteries.size()) {
            if(lo > 0) --lo;
            else if(hi < enteries.size()) ++hi;
        }
        set_status(to_string(tty_ws.ws_row)+" "+to_string(tty_ws.ws_col)+"\t"+to_string(lo)+" "+to_string(hi));
    }

    render();
}

void scroll_down() {
    if(selected_entry + 1 == enteries.size()) {
        set_status("Reached End", true);
        return;
    }
    ++selected_entry;
    if(selected_entry > hi) {
        ++hi;
        ++lo;
        render();
    } else {
        MOVE_CURSOR_DOWN();
    }
}

void scroll_up() {
    if(selected_entry == 0) {
        set_status("At Top", true);
        return;
    }
    --selected_entry;
    if(selected_entry < lo) {
        --hi;
        --lo;
        render();
    } else {
        MOVE_CURSOR_UP();
    }
}

void activate_normal_mode() {
    tty_config.c_lflag &= ~(ICANON | ECHO);
    tty_config.c_cc[VMIN] = 1;
    tcsetattr(fd, TCSANOW, &tty_config);

    mode = NORMAL_MODE;
}

void update() {
    /* Enter normal mode */
    activate_normal_mode();

    /* get contents of cwd */
    list();

    /* set status bar position */
    st_start.i = tty_ws.ws_row;
    st_start.j = 1;

    /* set display area */
    top = 1;
    bottom = tty_ws.ws_row - 1;

    lo = 0;
    hi = min(bottom, (int)enteries.size())-1;
    selected_entry = 0;

    render();
}

void go_back() {
    if(cwd == home) {
        set_status("At Root", true);
        return;
    } else {
        int pos = cwd.find_last_of('/');
        cwd = cwd.substr(0, pos);
        history.pop();
        /* clear lr history */
        while(!lr_history.empty())
            lr_history.pop();
        update();
    }
}

void go_left() {
    if(history.size() == 1) {
        set_status("At Left End", true);
        return;
    }

    lr_history.push(history.top());
    history.pop();
    cwd = history.top();
    update();
}

void go_right() {
    if(lr_history.empty()) {
        set_status("At Right End", true);
        return;
    }
    history.push(lr_history.top());
    lr_history.pop();
    cwd = history.top();
    update();
}

void enter() {
    entry e = enteries[selected_entry];
    if(e.permission[0] == 'd' && e.name != ".") {
        if(e.name == "..") {
            go_back();
        } else {
            cwd += "/"+e.name;
            history.push(cwd);
            update();
            /* clear lr history */
            while(!lr_history.empty())
                lr_history.pop();
        }
    }
}

/*
    Main function
*/
int main() {
    /* open /dev/tty */
    fd = open(tty, O_RDWR | O_NDELAY | O_NOCTTY);
    if(!isatty(fd)) {
        fprintf(stderr, "failed to open tty\n");
        return 0;
    }
    
    /* get the current tty configuration */
    if(tcgetattr(fd, &tty_config)<0) {
        fprintf(stderr, "unable to get tty config\n");
        return 0;
    }

    /* get window size config */
    if(ioctl(fd, TIOCGWINSZ, &tty_ws) < 0) {
        int error = errno;
        fprintf(stderr, "unable to get window size\n");
        fprintf(stderr, "%d\n", error);
        return 0;
    }

    /* set signal for window resize */
    signal(SIGWINCH, signal_win_resize);

    /* get home and current working directory */
    home = getcwd(NULL, 0);
    cwd = home;

    /* update contents */
    // DISABLE_LINE_WRAP();
    update();
    history.push(cwd);

    char ch;
    while(1) {
        cin.get(ch);
        set_status(to_string(ch), true);
        switch(ch) {
            case 'l': {
                scroll_down();
                break;
            }
            case 'k': {
                scroll_up();
                break;
            }
            case '\n': {
                enter();
                break;
            }
            case 127: {
                go_back();
                break;
            }
            case 'D': {
                go_left();
                break;
            }
            case 'C': {
                go_right();
                break;
            }
            default: break;
        }
    }


    // write(fd, cwd, strlen(cwd));
    // write(fd, "\n", 1);

    // DIR *temp = opendir(cwd);
    // struct dirent *ent;
    // while((ent = readdir(temp)) != NULL)
    //     printf("%s\n", ent->d_name);


    // tty_config.c_lflag &= ~(ECHO);
    // tcsetattr(fd, TCSANOW, &tty_config);

    // printf("winsize : %d %d\n", tty_ws.ws_row, tty_ws.ws_col);

    // write(fd, "hello\n", 6);

    // write(fd, "asdf\n", 5);
    // write(fd, "\033[H\033[J", 6);
    // // write(fd, "\e[1;1H\e[2J", 12);
}