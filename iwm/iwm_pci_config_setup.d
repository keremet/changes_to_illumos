#!/usr/sbin/dtrace -s

#pragma D option flowindent

fbt::pci_config_setup:entry
{
	self->follow = 1;
}

fbt::pci_config_setup:return
{
	self->follow = 0;
	trace(arg1);
}

fbt:::entry
/self->follow/
{
}

fbt:::return
/self->follow/
{
	trace(arg1);
}
