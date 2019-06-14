"""
Configure a SCons Environment to build against nc_server if available,
either within the source tree or through pkg-config.
"""

import os
import eol_scons.parseconfig as pc

# According to the scons man page, it is permissible to create multiple
# Configure contexts with the same config_h file.  SCons will concatenate
# the config settings in that file.


def nc_server(env):
    # It seems like for cross-builds the PKG_CONFIG_PATH needs to be adjusted
    # accordingly, or maybe the path to pkg-config needs to be set, such as
    # /opt/arcom/bin/armbe-linux-pkg-config.  However, the closest thing to
    # this I've found is in scripts/build_nidas.sh.  Maybe that kind of
    # customization should be in the scons arm*cross tools.
    
    # If nc-server exists in the source tree, then use it.
    local_nc_server = env.get('NIDAS_NC_SERVER_BUILD', False)

    arch = env.get('ARCH', '')
    conf = env.Configure(conf_dir=".sconf_temp",
                         log_file='#/config' + arch +'.log',
                         config_h="include/nidas/Config.h")

    if local_nc_server:
        print("Applying nc_server_client tool to use nc-server in source tree.")
        env.Require('nc_server_client')
        # Have to define the config symbol explicitly because the nc_server
        # libraries have not been built yet, so there's no point running a
        # normal configure check for them.
        conf.Define('HAVE_LIBNC_SERVER_RPC', 1,
                    "nc-server built inside the nidas source tree")

    else:
        print("Using pkg-config to find nc_server.")
        pc.ParseConfig(conf.env, 'pkg-config --cflags --libs nc_server')
        if conf.CheckLibWithHeader('nc_server_rpc', 'nc_server_rpc.h','C++',
                                   'xdr_connection(0,0);'):
            # append nc_server library to build environment
            # I don't grok all the issues related to using RPATH. Debian
            # advises against it in most, but not all situations. We'll
            # enable it here and see what happens...
            env.AppendUnique(CPPPATH=conf.env['CPPPATH'])
            env.AppendUnique(LIBS=conf.env['LIBS'],LIBPATH=conf.env['LIBPATH'],
                             RPATH=conf.env['LIBPATH'])

    conf.Finish()
    
Export('nc_server')
