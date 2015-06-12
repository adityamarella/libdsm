import sys
import paramiko


class TestRunner(object):

    def __init__(self, key_filepath, nodes):
        self.key_filepath = key_filepath
        self.nodes = nodes
        self.conns = self.connect(nodes)

    def connect(self, nodes):
        conns = []
        for node in nodes:
            print "Opening ssh into %s"%node[1]
            conn = paramiko.SSHClient()
            conn.load_system_host_keys()
            conn.set_missing_host_key_policy(paramiko.AutoAddPolicy()) 
            conn.connect(node[1], username="ubuntu", key_filename=self.key_filepath)
            conns.append(conn)
        return conns

    def close(self):
        for conn in self.conns:
            conn.close()

    def exec_command(self, conn, command):
        print("Executing: %s"%command)
        stdin, stdout, stderr = conn.exec_command(command)
        return stdin, stdout, stderr

    def ssout(self, stream):
        for line in stream:
            sys.stdout.write(line)

    def test_setup(self):
        for i, conn in enumerate(self.conns):
            stdin, stdout, stderr = conn.exec_command('sudo aptitude update && sudo aptitude -y upgrade && sudo aptitude -y install build-essential git make automake pkg-config libbsd-dev')
            self.ssout(stdout)
            self.ssout(stderr)

    def test_keygen(self):
        for i, conn in enumerate(self.conns):
            stdin, stdout, stderr = conn.exec_command('ssh-keygen -t rsa -b 4096 -C "aditya.marella@gmail.com"')
            stdin.write("\n")
            stdin.write("dsm")
            stdin.write("dsm")
            stdin.close()
            
            stdin, stdout, stderr = conn.exec_command('cat ~/.ssh/id_rsa.pub')
            print self.nodes[i][1], stdout.readlines()
            
            #stdin, stdout, stderr = conn.exec_command('cd dsm && git pull && make && make test')
    
    def test_pull_and_compile(self):
        for i, conn in enumerate(self.conns):
            print "\n"
            print "Fetching code and compiling on host %s"%self.nodes[i][1]
            stdin, stdout, stderr = self.exec_command(conn, 'cd dsm && git pull')
            self.ssout(stdout)
            self.ssout(stderr)
            
            print "Cleaning.."
            stdin, stdout, stderr = self.exec_command(conn, 'cd dsm && make clean')
            self.ssout(stdout)
            self.ssout(stderr)

            print "Creating conf file.."
            stdin, stdout, stderr = self.exec_command(conn, 'cd dsm && echo "%s" > dsm.conf'%(self.test_create_conf_data()))
            self.ssout(stdout)
            self.ssout(stderr)
            
            print "Compiling.."
            stdin, stdout, stderr = self.exec_command(conn, 'cd dsm && make clean && make && make test')
            self.ssout(stdout)
            self.ssout(stderr)
            
    def test_create_conf_data(self):
        conf_data = ['%s'%len(self.nodes)]
        for i, node in enumerate(self.nodes):
            if node[0] == 1:
                node[0] = '*'
            else:
                node[0] = "-"
            
            conf_data.append(' '.join([str(item) for item in node])) 
        
            if node[0] == '*':
                node[0] = 1
            else:
                node[0] = 0
        
        return '\n'.join(conf_data)
        
    def test_run(self):

        for i, conn in enumerate(self.conns):
            #generate matrix
            stdin, stdout, stderr = self.exec_command(conn, 'dsm/bin/matrix_gen 10 10 10 10')
            self.ssout(stdout)
            self.ssout(stderr)

        for i, conn in enumerate(self.conns):
            #run matrix multiplication
            node = self.nodes[i]
            stdin, stdout, stderr = self.exec_command(conn, 'dsm/bin/matrix_mul %d %s %d'%(node[0], node[1], node[2]))
            #script won't block here
            #whether this command failed or succeeded this will go forward

        # fetch back the result from the a file on master?

if __name__=='__main__':
    nodes = [
        [1, 'ec2-52-68-199-206.ap-northeast-1.compute.amazonaws.com', 8000],
        [0, 'ec2-52-68-33-100.ap-northeast-1.compute.amazonaws.com', 8001],
        [0, 'ec2-52-68-41-177.ap-northeast-1.compute.amazonaws.com', 8002],
        [0, 'ec2-52-68-131-223.ap-northeast-1.compute.amazonaws.com', 8003],
        [0, 'ec2-52-68-155-141.ap-northeast-1.compute.amazonaws.com', 8004],
        [0, 'ec2-52-68-174-74.ap-northeast-1.compute.amazonaws.com', 8005],
        [0, 'ec2-52-69-9-94.ap-northeast-1.compute.amazonaws.com', 8006],
        [0, 'ec2-52-68-87-236.ap-northeast-1.compute.amazonaws.com', 8007]
    ]
    
    t = TestRunner(sys.argv[1], nodes)

    t.test_pull_and_compile()

    #t.test_run()
     
    t.close()

