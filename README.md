# vers

## git问题
1. 学习成本高，概念复杂。版本管理是任何一个普通人能理解、大部分时间通过记忆就能完成的工作，在复杂一些的情形下，也理应能够通过几句简单的语言记录完成版本管理，不需要花时间学习git中种种复杂的概念、语法、功能。
2. 次要问题是diff算法呈现出来的diff结果，不能准确地展现修改过程。

## vers核心观点
让用户最大程度地忽略版本管理的过程。用户对版本管理的诉求只有两点：备份，记录文件演变，达成如此简单的目的，用户的必要操作只有下载最新文件和提交，其他所有的管理工作应该由版本管理工具完成，不应该被用户感知到。也就是说，用户使用vers完成版本管理，只需要懂得两个命令：vers dowload和vers upload，下载和上传，就可以查看文件的演变历程。如果用户想要把文件切换到历史版本，才要用到额外的命令：vers goto。

## vers基本特征

![image](https://github.com/user-attachments/assets/c55a2854-713b-44eb-829d-ed6c62b6faff)

1. **保留git本地仓和远程仓概念**<br/>
本地仓表示文件在当前设备上的备份，修改中的版本（editing version）；远程仓表示文件的一个正式版本（official version），1个或多个持有editing version的用户，每完成1次vers upload后，形成新的official version。
2. **取消本地仓的暂存区和版本库**<br/>
暂存区和版本库是本地仓管理复杂化的罪魁祸首，git要求用户在自己的设备上完成对文件的修改后，通过git add追踪文件、将文件加入暂存区，还要求用户通过commit命令告知git修改已经生效，否则我们的修改就会停在暂存区中，这是典型的增加用户负担的行为。在vers删除暂存区和版本库的概念，当用户完成对文件的修改，在点击ctrl+S那一刻，本地仓的版本管理就已经完成了，不需要告诉vers任何事。
3. **仅发生upload才会形成commit点**<br/>
git中每次从暂存区到版本库的commit都会被记作一次commit点，这是复杂版本管理的负面产物。很多用户在无法理解在什么样的时机commit合适或将什么级别的代码量作为1次commit合适，这就意味着用户的每次push可能包含着大量的修改甚微的、无意义的commit。因此当我们下载一个新的、庞大的项目时，往往面对的是长篇小说一样的commit记录，最糟糕的事是，很多人在commit中不会标注详细信息，导致我们在浏览log时不得不对抗每个用户负面的git使用习惯，甚至大海捞针一般去找那个想要的commit点。vers从功能上直接拒绝用户频繁的、无意义的commit操作，仅当用户完成一次彻底的本地仓修改，并执行vers upload操作后，才能生成一条upload record（对应git中commit点），所有upload record组成official version的offical log。
4. **本地仓的版本管理以文件粒度进行**<br/>
核心观点3意味着official version不会关注本地仓的修改细节，每条upload record是1次对本地仓的完整修改。这样看起来，用户没办法在本地仓中去回滚文件了，因为在upload之前，对本地仓的修改不会生成record。这是不合理也无法满足用户的基本诉求的，所以对于本地仓，vers不会通过record来完成回滚。对管理范围的每一个文件，vers都维护一个修改editing log，用户可以通过vers goto命令回退或切换到log中的任意一个修改点（point）完成文件粒度的切换。完成upload后，editing log不会被带入offical log，只能保留在本地。当我们下载一个完整的official version时，也不会得到历史upload的editing log，editing log是崭新的、空白的，因为它的任务只是记录本地仓的修改。
5. **新的diff算法**<br/>
