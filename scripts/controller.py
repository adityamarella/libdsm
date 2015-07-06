import sys
import boto3
import time

def start_instances(N):
    ec2 = boto3.resource("ec2")
    instances = list(ec2.instances.filter(
                Filters=[{'Name': 'instance-state-name'}]))
    if len(instances) < N:
        count = N - len(instances)
        instances = ec2.create_instances(ImageId='ami-df6a8b9b', 
                MinCount=count, MaxCount=count, 
                KeyName='ec2-dsm-adi-key', InstanceType='t2.micro')
    for instance in instances:
        instance.wait_until_running()

def terminate_instances():
    ec2 = boto3.resource("ec2")
    instances = ec2.instances.filter(
            Filters=[{'Name': 'instance-state-name', 'Values': ['running']}])
    for instance in instances:
        instance.terminate()

def get_running_instances():
    ec2 = boto3.resource("ec2")
    instances = ec2.instances.filter(
            Filters=[{'Name': 'instance-state-name', 'Values': ['running']}])
    return [instance.public_dns_name for instance in instances]

if __name__=='__main__':
    terminate_instances()


