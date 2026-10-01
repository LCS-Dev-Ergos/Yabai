// yabai-msg: `yabai -m` on its own. It links no framework, so it starts in
// about the time of an empty program, and it is signed like yabai, so the
// daemon accepts it.
#include "client.c"

int main(int argc, char **argv)
{
    return client_send_message(argc, argv);
}
